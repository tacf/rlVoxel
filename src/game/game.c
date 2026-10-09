#include "game/game.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <raymath.h>
#include <stdlib.h>

#include "constants.h"
#include "gfx/clouds.h"
#include "gfx/renderer.h"
#include "gfx/selection_highlight.h"
#include "game/game_input.h"
#include "game/player.h"
#include "game/raycast.h"
#include "net/net.h"
#include "net/protocol.h"
#include "raylib.h"
#include "rlcimgui.h"
#include "server/server_core.h"
#include "ui/clay_render.h"
#include "ui/font_atlas.h"
#include "ui/hud.h"
#include <clay.h>
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "world/blocks.h"
#include "world/mesher.h"
#include "world/world.h"
#include "profiling/profiler.h"
#include "profiling/profiler_renderer.h"
#include "diagnostics/net_profiler.h"
#include "diagnostics/telemetry.h"

#define GAME_LOOK_SPIKE_LIMIT 180.0f

/*
 * Reconciliation tuning.
 * Rebuild only when server/prediction drift is meaningful.
 */
#define GAME_RECONCILE_IGNORE_HORIZONTAL 0.12f
#define GAME_RECONCILE_IGNORE_VERTICAL 0.08f

/*
 * Input replication policy:
 * - C2S_InputCmd is action-oriented (click edges + selected block authority).
 * - C2S_PlayerMove carries movement pose updates.
 */
#define GAME_INPUT_KEEPALIVE_TICKS 20u
#define GAME_MOVE_KEEPALIVE_TICKS 20u
#define GAME_MOVE_POSITION_EPSILON 0.0005f
#define GAME_MOVE_VELOCITY_EPSILON 0.0010f
#define GAME_MOVE_ANGLE_EPSILON 0.0005f
#define GAME_SURVIVAL_BREAK_PULSE_TICKS 5u

/*
 * Local (server idependent) break animation.
 */
#define GAME_BREAK_VISUAL_STAGES 10
#define GAME_BREAK_VISUAL_HOLD_PROGRESS 0.999f
#define GAME_BREAK_VISUAL_MIN_SECONDS 0.22f
#define GAME_BREAK_VISUAL_PER_DURABILITY_SECONDS 0.06f
#define GAME_BREAK_RECOVERY_DELAY_TICKS 10u
#define GAME_BREAK_RECOVERY_INTERVAL_TICKS 5u
#define GAME_BREAK_VISUAL_DECAY_STEP 0.14f

/* Build raylib camera vectors from a player pose (position + yaw/pitch). */
static void set_camera_from_pose(Game *game, Vector3 position, float yaw, float pitch) {
  Vector3 eye = {position.x, position.y + PLAYER_EYE_HEIGHT, position.z};

  game->camera.position = eye;
  game->camera.target = Vector3Add(eye, Player_DirectionFromAngles(yaw, pitch));
  game->camera.up = (Vector3){0.0f, 1.0f, 0.0f};
}

/* Snap camera directly to the player's current simulation pose. */
static void sync_camera(Game *game) {
  set_camera_from_pose(game, game->player.position, game->view_yaw, game->view_pitch);
}

/* Interpolate position only; keep look immediate from local view yaw/pitch. */
static void sync_camera_interpolated(Game *game, float alpha) {
  alpha = Clamp(alpha, 0.0f, 1.0f);
  Vector3 position = Vector3Lerp(game->player.previous_position, game->player.position, alpha);
  set_camera_from_pose(game, position, game->view_yaw, game->view_pitch);
}

static void game_reset_break_visual(Game *game) {
  game->break_visual_active = false;
  game->break_visual_x = 0;
  game->break_visual_y = 0;
  game->break_visual_z = 0;
  game->break_visual_damage = 0.0f;
  game->break_visual_last_tick = 0u;
  game->break_visual_last_recover_tick = 0u;
}

static void set_cursor_locked(Game *game, bool locked) {
  bool was_locked = game->cursor_locked;
  game->cursor_locked = locked;

  if (locked) {
    DisableCursor();
    if (!was_locked) {
      game->suppress_next_frame_look = true;
    }
  } else {
    EnableCursor();
  }
}

static bool game_is_paused(Game *game) { return game->show_escape_menu || game->show_options; }

static void open_escape_menu(Game *game) {
  if (game_is_paused(game)) {
    return;
  }

  game->show_escape_menu = true;
  game->lock_before_escape_menu = game->cursor_locked || game->pending_lock_request;
  game->pending_lock_request = false;
  set_cursor_locked(game, false);
}

static void close_escape_menu(Game *game) {
  if (!game_is_paused(game)) {
    return;
  }

  game->show_escape_menu = false;
  game->show_options = false;
  if (game->lock_before_escape_menu) {
    game->pending_lock_request = true;
    set_cursor_locked(game, true);
  } else {
    game->pending_lock_request = false;
    set_cursor_locked(game, false);
  }
}

static void game_reset_prediction_history(Game *game) {
  memset(game->prediction_history, 0, sizeof(game->prediction_history));
  memset(game->input_history, 0, sizeof(game->input_history));
}

static void game_reset_send_state(Game *game) {
  game->has_last_sent_move = false;
  game->last_sent_move_tick_id = 0u;
  game->has_last_sent_input_cmd = false;
  game->last_sent_input_tick_id = 0u;
  game->last_survival_break_send_tick = 0u;
}

static void game_reset_session_state(Game *game) {
  game->has_authoritative_state = false;
  game->last_authoritative_tick = 0;
  game_reset_send_state(game);
  game_reset_prediction_history(game);
}

static void game_record_send(const Game *game, NetMessageType type, uint32_t sequence,
                             uint8_t channel, uint32_t confirm_ref) {
  size_t packet_size = Protocol_FixedPacketSize(type);
  if (packet_size == 0u) {
    packet_size = Protocol_HeaderSize();
  }
  NetProfiler_RecordSend(type, sequence, game->network_tick_counter, packet_size, channel,
                         confirm_ref);
}

static bool game_send_hello(Game *game, int requested_render_distance) {
  NetHello hello = {
      .requested_render_distance = (requested_render_distance > 0)
                                       ? (uint32_t)requested_render_distance
                                       : (uint32_t)DEFAULT_RENDER_DISTANCE,
  };
  uint32_t sequence = game->network_sequence++;

  if (!Net_SendHello(game->net, sequence, game->network_tick_counter, &hello)) {
    return false;
  }
  game_record_send(game, NET_MSG_C2S_HELLO, sequence, 0u, 0u);
  return true;
}

static bool game_send_input(Game *game, const GameplayInputCmd *cmd) {
  if (!game->connected || !game->welcome_received) {
    return false;
  }

  uint32_t sequence = game->network_sequence++;
  if (!Net_SendInputCmd(game->net, sequence, game->network_tick_counter, cmd)) {
    return false;
  }
  game_record_send(game, NET_MSG_C2S_INPUT_CMD, sequence, 1u, cmd->tick_id);
  return true;
}

static bool game_send_player_move(Game *game, const NetPlayerMove *move) {
  if (!game->connected || !game->welcome_received) {
    return false;
  }

  uint32_t sequence = game->network_sequence++;
  if (!Net_SendPlayerMove(game->net, sequence, game->network_tick_counter, move)) {
    return false;
  }
  game_record_send(game, NET_MSG_C2S_PLAYER_MOVE, sequence, 1u, move->tick_id);
  return true;
}

static bool game_input_cmd_semantic_equal(const GameplayInputCmd *a, const GameplayInputCmd *b) {
  uint8_t a_action_buttons;
  uint8_t b_action_buttons;

  a_action_buttons = a->buttons & (uint8_t)(GAMEPLAY_INPUT_LEFT_CLICK | GAMEPLAY_INPUT_RIGHT_CLICK);
  b_action_buttons = b->buttons & (uint8_t)(GAMEPLAY_INPUT_LEFT_CLICK | GAMEPLAY_INPUT_RIGHT_CLICK);
  return a_action_buttons == b_action_buttons && a->selected_block == b->selected_block &&
         a->gameplay_mode == b->gameplay_mode && a->fly_enabled == b->fly_enabled;
}

static bool game_should_send_input(const Game *game, const GameplayInputCmd *cmd) {
  uint32_t ticks_since_last_send;

  if (!game->has_last_sent_input_cmd) {
    return true;
  }

  /* Action commands are sparse; movement continuity is handled by C2S_PlayerMove. */

  if (!game_input_cmd_semantic_equal(cmd, &game->last_sent_input_cmd)) {
    return true;
  }

  ticks_since_last_send = cmd->tick_id - game->last_sent_input_tick_id;
  return ticks_since_last_send >= GAME_INPUT_KEEPALIVE_TICKS;
}

static void game_mark_input_sent(Game *game, const GameplayInputCmd *cmd) {
  game->last_sent_input_cmd = *cmd;
  game->last_sent_input_tick_id = cmd->tick_id;
  game->has_last_sent_input_cmd = true;
}

static void game_build_player_move(const Game *game, uint32_t tick_id, NetPlayerMove *out_move) {
  *out_move = (NetPlayerMove){
      .tick_id = tick_id,
      .position_x = game->player.position.x,
      .position_y = game->player.position.y,
      .position_z = game->player.position.z,
      .velocity_x = game->player.velocity.x,
      .velocity_y = game->player.velocity.y,
      .velocity_z = game->player.velocity.z,
      .yaw = game->player.yaw,
      .pitch = game->player.pitch,
      .on_ground = game->player.on_ground ? 1u : 0u,
  };
}

static bool game_player_move_semantic_equal(const NetPlayerMove *a, const NetPlayerMove *b) {
  return fabsf(a->position_x - b->position_x) <= GAME_MOVE_POSITION_EPSILON &&
         fabsf(a->position_y - b->position_y) <= GAME_MOVE_POSITION_EPSILON &&
         fabsf(a->position_z - b->position_z) <= GAME_MOVE_POSITION_EPSILON &&
         fabsf(a->velocity_x - b->velocity_x) <= GAME_MOVE_VELOCITY_EPSILON &&
         fabsf(a->velocity_y - b->velocity_y) <= GAME_MOVE_VELOCITY_EPSILON &&
         fabsf(a->velocity_z - b->velocity_z) <= GAME_MOVE_VELOCITY_EPSILON &&
         fabsf(a->yaw - b->yaw) <= GAME_MOVE_ANGLE_EPSILON &&
         fabsf(a->pitch - b->pitch) <= GAME_MOVE_ANGLE_EPSILON && a->on_ground == b->on_ground;
}

static bool game_should_send_player_move(const Game *game, const NetPlayerMove *move) {
  uint32_t ticks_since_last_send;

  if (!game->has_last_sent_move) {
    return true;
  }

  if (!game_player_move_semantic_equal(move, &game->last_sent_move)) {
    return true;
  }

  ticks_since_last_send = move->tick_id - game->last_sent_move_tick_id;
  return ticks_since_last_send >= GAME_MOVE_KEEPALIVE_TICKS;
}

static void game_mark_player_move_sent(Game *game, const NetPlayerMove *move) {
  game->last_sent_move = *move;
  game->last_sent_move_tick_id = move->tick_id;
  game->has_last_sent_move = true;
}

static float game_network_tick_dt(const Game *game) {
  int rate;
  rate = (game->network_tick_rate > 0) ? game->network_tick_rate : (int)GAME_TICK_RATE;
  return 1.0f / (float)rate;
}

static const PredictedPlayerSample *game_prediction_find(const Game *game, uint32_t tick_id) {
  const PredictedPlayerSample *slot =
      &game->prediction_history[tick_id % GAME_PREDICTION_HISTORY_SIZE];
  return (slot->valid && slot->tick_id == tick_id) ? slot : NULL;
}

static void game_store_input_cmd(Game *game, const GameplayInputCmd *cmd) {
  game->input_history[cmd->tick_id % GAME_INPUT_HISTORY_SIZE] = (GameplayInputHistoryEntry){
      .tick_id = cmd->tick_id,
      .cmd = *cmd,
      .valid = true,
  };
}

static const GameplayInputCmd *game_find_input_cmd(const Game *game, uint32_t tick_id) {
  const GameplayInputHistoryEntry *slot = &game->input_history[tick_id % GAME_INPUT_HISTORY_SIZE];
  return (slot->valid && slot->tick_id == tick_id) ? &slot->cmd : NULL;
}

static void game_store_prediction_sample(Game *game, uint32_t tick_id) {
  game->prediction_history[tick_id % GAME_PREDICTION_HISTORY_SIZE] = (PredictedPlayerSample){
      .tick_id = tick_id,
      .position = game->player.position,
      .velocity = game->player.velocity,
      .on_ground = game->player.on_ground,
      .valid = true,
  };
}

static void game_apply_simulation_cmd(Game *game, const GameplayInputCmd *cmd, float tick_dt) {
  GameInputSnapshot sim = {0};

  if (tick_dt <= 0.0f) {
    return;
  }

  sim.move_forward = (cmd->buttons & GAMEPLAY_INPUT_MOVE_FORWARD) != 0;
  sim.move_backward = (cmd->buttons & GAMEPLAY_INPUT_MOVE_BACKWARD) != 0;
  sim.move_left = (cmd->buttons & GAMEPLAY_INPUT_MOVE_LEFT) != 0;
  sim.move_right = (cmd->buttons & GAMEPLAY_INPUT_MOVE_RIGHT) != 0;
  sim.sprint = (cmd->buttons & GAMEPLAY_INPUT_SPRINT) != 0;
  sim.jump_held = (cmd->buttons & GAMEPLAY_INPUT_JUMP_HELD) != 0;
  sim.mouse_delta = (Vector2){cmd->look_delta_x, cmd->look_delta_y};

  game->player.selected_block = cmd->selected_block;
  game->player.gameplay_mode = (cmd->gameplay_mode == (uint8_t)GAMEPLAY_MODE_SURVIVAL)
                                   ? GAMEPLAY_MODE_SURVIVAL
                                   : GAMEPLAY_MODE_CREATIVE;
  game->player.fly_enabled =
      (game->player.gameplay_mode == GAMEPLAY_MODE_CREATIVE) && (cmd->fly_enabled != 0u);
  Player_Update(&game->player, &game->world, &sim, tick_dt, true, game->player.fly_enabled);
  game_store_prediction_sample(game, cmd->tick_id);
}

static void game_predict_local_player(Game *game, const GameplayInputCmd *cmd, float tick_dt) {
  if (!game->connected || !game->welcome_received) {
    return;
  }

  game_apply_simulation_cmd(game, cmd, tick_dt);
}

static void game_apply_authoritative_state(Game *game, const AuthoritativePlayerState *state,
                                           bool sync_orientation) {
  game->player.previous_position = game->player.position;
  game->player.previous_yaw = game->player.yaw;
  game->player.previous_pitch = game->player.pitch;

  game->player.position = (Vector3){state->position_x, state->position_y, state->position_z};
  game->player.velocity = (Vector3){state->velocity_x, state->velocity_y, state->velocity_z};
  if (sync_orientation) {
    game->player.yaw = state->yaw;
    game->player.pitch = state->pitch;
  }
  game->player.on_ground = state->on_ground != 0;

  game->last_authoritative_tick = state->input_tick_id;
  game->has_authoritative_state = true;

  World_SetTime(&game->world, state->world_time);
}

static void game_replay_unacked_inputs(Game *game, uint32_t acknowledged_input_tick) {
  float tick_dt = game_network_tick_dt(game);

  for (uint32_t tick = acknowledged_input_tick + 1u; tick < game->network_tick_counter; tick++) {
    const GameplayInputCmd *cmd = game_find_input_cmd(game, tick);
    if (cmd != NULL) {
      game_apply_simulation_cmd(game, cmd, tick_dt);
    }
  }
}

static void game_accept_authoritative_motion(Game *game, const AuthoritativePlayerState *state) {
  game->player.velocity = (Vector3){state->velocity_x, state->velocity_y, state->velocity_z};
  game->player.on_ground = state->on_ground != 0;
  World_SetTime(&game->world, state->world_time);
}

static void game_reconcile_player_state(Game *game, const AuthoritativePlayerState *state) {
  uint32_t acknowledged_input_tick = state->input_tick_id;

  if (!game->has_authoritative_state) {
    game_apply_authoritative_state(game, state, true);
    game_replay_unacked_inputs(game, acknowledged_input_tick);
    return;
  }

  if (acknowledged_input_tick < game->last_authoritative_tick) {
    return;
  }

  if (acknowledged_input_tick == game->last_authoritative_tick) {
    /*
     * Do not re-correct against the same ack repeatedly.
     * This is a common source of visible side-to-side micro jitter.
     */
    game_accept_authoritative_motion(game, state);
    return;
  }

  const PredictedPlayerSample *predicted = game_prediction_find(game, acknowledged_input_tick);
  if (predicted != NULL) {
    Vector3 authoritative_pos = {state->position_x, state->position_y, state->position_z};
    Vector3 delta = Vector3Subtract(authoritative_pos, predicted->position);
    float horizontal_error = sqrtf(delta.x * delta.x + delta.z * delta.z);
    float vertical_error = fabsf(delta.y);

    if (horizontal_error <= GAME_RECONCILE_IGNORE_HORIZONTAL &&
        vertical_error <= GAME_RECONCILE_IGNORE_VERTICAL) {
      game->last_authoritative_tick = acknowledged_input_tick;
      game_accept_authoritative_motion(game, state);
      return;
    }
  }

  game_apply_authoritative_state(game, state, false);
  game_replay_unacked_inputs(game, acknowledged_input_tick);
}

static void game_process_network(Game *game) {
  NetEvent event;
  AuthoritativePlayerState latest_player_state = {0};
  bool has_latest_player_state = false;

  Net_Update(game->net, 0);

  while (Net_PollEvent(game->net, &event)) {
    if (event.type == NET_EVENT_CONNECTED) {
      game->connected = true;
      game_reset_send_state(game);
      game_send_hello(game, game->world.render_distance);
    } else if (event.type == NET_EVENT_DISCONNECTED) {
      game->connected = false;
      game->welcome_received = false;
      game_reset_session_state(game);
    } else if (event.type == NET_EVENT_MESSAGE) {
      uint32_t confirm_ref = 0u;
      if (event.message_type == NET_MSG_S2C_PLAYER_STATE) {
        confirm_ref = event.payload.player_state.input_tick_id;
      }
      NetProfiler_RecordReceive(event.message_type, event.header.sequence, event.header.tick,
                                event.packet_size, event.channel, confirm_ref);

      switch (event.message_type) {
      case NET_MSG_S2C_WELCOME:
        game->welcome_received = true;
        game->connected = true;
        game_reset_session_state(game);
        game->seed = event.payload.welcome.seed;
        game->network_tick_rate = (event.payload.welcome.tick_rate > 0)
                                      ? event.payload.welcome.tick_rate
                                      : (int)GAME_TICK_RATE;
        if (event.payload.welcome.render_distance >= 2) {
          game->world.render_distance = event.payload.welcome.render_distance;
        }
        break;

      case NET_MSG_S2C_PLAYER_STATE:
        /*
         * When chunk streaming stalls the server thread, multiple snapshots can
         * arrive in one client tick. Apply only the newest to avoid visible
         * micro-corrections (especially vertical "step" jitter).
         */
        if (!has_latest_player_state ||
            event.payload.player_state.input_tick_id > latest_player_state.input_tick_id ||
            (event.payload.player_state.input_tick_id == latest_player_state.input_tick_id &&
             event.payload.player_state.tick_id >= latest_player_state.tick_id)) {
          latest_player_state = event.payload.player_state;
          has_latest_player_state = true;
        }
        break;

      case NET_MSG_S2C_CHUNK_DATA:
        World_ApplyChunkData(
            &game->world, event.payload.chunk_data.cx, event.payload.chunk_data.cz,
            event.payload.chunk_data.blocks, sizeof(event.payload.chunk_data.blocks),
            event.payload.chunk_data.skylight, sizeof(event.payload.chunk_data.skylight),
            event.payload.chunk_data.heightmap, sizeof(event.payload.chunk_data.heightmap));
        break;

      case NET_MSG_S2C_BLOCK_DELTA:
        World_ApplyBlockDelta(&game->world, event.payload.block_delta.x,
                              event.payload.block_delta.y, event.payload.block_delta.z,
                              event.payload.block_delta.block_id,
                              event.payload.block_delta.skylight);
        if (game->break_visual_active && game->break_visual_x == event.payload.block_delta.x &&
            game->break_visual_y == event.payload.block_delta.y &&
            game->break_visual_z == event.payload.block_delta.z) {
          game_reset_break_visual(game);
        }
        break;

      case NET_MSG_S2C_CHUNK_UNLOAD:
        World_RemoveChunk(&game->world, event.payload.chunk_unload.cx,
                          event.payload.chunk_unload.cz);
        break;

      case NET_MSG_S2C_DISCONNECT:
        game->quit_requested = true;
        break;

      default:
        break;
      }
    }
  }

  if (has_latest_player_state) {
    game_reconcile_player_state(game, &latest_player_state);
  }
}

static Vector3 game_view_direction(const Game *game) {
  return Player_DirectionFromAngles(game->view_yaw, game->view_pitch);
}

static bool game_raycast_target_block(const Game *game, VoxelRaycastHit *out_hit) {
  Vector3 eye;
  Vector3 dir;

  eye = Player_GetEyePosition(&game->player);
  dir = game_view_direction(game);
  return Raycast_VoxelForPlacement(&game->world, eye, dir, PLAYER_REACH_DISTANCE, out_hit) &&
         out_hit->hit;
}

static float game_break_visual_duration_seconds(uint8_t block_id) {
  int durability;

  durability = Block_GetDurability(block_id);
  if (durability <= 0) {
    return 0.0f;
  }

  return Clamp(GAME_BREAK_VISUAL_MIN_SECONDS +
                   (float)durability * GAME_BREAK_VISUAL_PER_DURABILITY_SECONDS,
               GAME_BREAK_VISUAL_MIN_SECONDS, 1.20f);
}

static void game_decay_break_visual(Game *game) {
  uint32_t recovery_start_tick;
  uint32_t elapsed_recovery_ticks;
  uint32_t recovery_steps;

  if (!game->break_visual_active) {
    return;
  }

  recovery_start_tick = game->break_visual_last_tick + GAME_BREAK_RECOVERY_DELAY_TICKS;
  if (game->network_tick_counter < recovery_start_tick) {
    return;
  }

  if (game->break_visual_last_recover_tick < recovery_start_tick) {
    game->break_visual_last_recover_tick = recovery_start_tick;
  }

  if (game->network_tick_counter <
      game->break_visual_last_recover_tick + GAME_BREAK_RECOVERY_INTERVAL_TICKS) {
    return;
  }

  elapsed_recovery_ticks = game->network_tick_counter - game->break_visual_last_recover_tick;
  recovery_steps = elapsed_recovery_ticks / GAME_BREAK_RECOVERY_INTERVAL_TICKS;
  game->break_visual_damage -= (float)recovery_steps * GAME_BREAK_VISUAL_DECAY_STEP;
  game->break_visual_last_recover_tick += recovery_steps * GAME_BREAK_RECOVERY_INTERVAL_TICKS;

  if (game->break_visual_damage <= 0.0f) {
    game_reset_break_visual(game);
  }
}

static void game_update_break_visual(Game *game, const VoxelRaycastHit *hit, float tick_dt) {
  int durability;
  float duration_seconds;
  float progress_step;

  if (!hit->hit || tick_dt <= 0.0f) {
    return;
  }

  if (hit->block_id == BLOCK_BEDROCK) {
    game_reset_break_visual(game);
    return;
  }

  durability = Block_GetDurability(hit->block_id);
  if (durability <= 0) {
    game_reset_break_visual(game);
    return;
  }

  if (!game->break_visual_active || game->break_visual_x != hit->block_x ||
      game->break_visual_y != hit->block_y || game->break_visual_z != hit->block_z) {
    game->break_visual_active = true;
    game->break_visual_x = hit->block_x;
    game->break_visual_y = hit->block_y;
    game->break_visual_z = hit->block_z;
    game->break_visual_damage = 0.0f;
    game->break_visual_last_recover_tick = 0u;
  }

  duration_seconds = game_break_visual_duration_seconds(hit->block_id);
  if (duration_seconds <= 0.0f) {
    return;
  }

  progress_step = tick_dt / duration_seconds;
  if (!isfinite(progress_step) || progress_step <= 0.0f) {
    return;
  }

  game->break_visual_damage =
      Clamp(game->break_visual_damage + progress_step, 0.0f, GAME_BREAK_VISUAL_HOLD_PROGRESS);
  game->break_visual_last_tick = game->network_tick_counter;
}

static int game_break_visual_stage(const Game *game) {
  uint8_t block_id;
  int durability;
  float ratio;
  int stage;

  if (!game->break_visual_active) {
    return -1;
  }

  block_id = World_GetBlock(&game->world, game->break_visual_x, game->break_visual_y,
                            game->break_visual_z);
  durability = Block_GetDurability(block_id);
  if (durability <= 0) {
    return -1;
  }

  ratio = Clamp(game->break_visual_damage, 0.0f, GAME_BREAK_VISUAL_HOLD_PROGRESS);
  stage = (int)floorf(ratio * (float)GAME_BREAK_VISUAL_STAGES);

  if (stage < 0) {
    stage = 0;
  }
  if (stage >= GAME_BREAK_VISUAL_STAGES) {
    stage = GAME_BREAK_VISUAL_STAGES - 1;
  }

  return stage;
}

static void game_draw_target_block_highlight(Game *game, const Camera3D *camera) {
  int break_stage;
  VoxelRaycastHit hit;
  (void)camera;

  if (!game->cursor_locked || game_is_paused(game)) {
    return;
  }

  if (game_raycast_target_block(game, &hit)) {
    SelectionHighlight_Draw(&hit);
  }

  if (game->gameplay_mode == GAMEPLAY_MODE_SURVIVAL && game->break_visual_active) {
    break_stage = game_break_visual_stage(game);
    if (break_stage >= 0) {
      SelectionHighlight_DrawDamageOverlay(game->terrain_texture, game->break_visual_x,
                                           game->break_visual_y, game->break_visual_z, break_stage);
    }
  }
}

typedef struct GameWorldPassContext {
  Game *game;
  float ambient;
} GameWorldPassContext;

static void game_draw_world_pass(void *ctx, const Camera3D *camera) {
  GameWorldPassContext *pass = (GameWorldPassContext *)ctx;
  if (pass->game->clouds_enabled) {
    Clouds_Draw(&pass->game->clouds, camera, pass->ambient);
  }
  World_Draw(&pass->game->world, camera, pass->ambient);
  game_draw_target_block_highlight(pass->game, camera);
}

static void game_draw_hud_pass(void *ctx) {
  Game *game = (Game *)ctx;
  Game_DrawHUD(game);
}

static void game_draw_debug_ui(Game *game) {
  if (!game->show_debug_menu) {
    return;
  }

  rligBegin();

  if (igBeginMainMenuBar()) {
    if (igBeginMenu("Debug", true)) {
      igMenuItem_BoolPtr("Profiler Statistics", NULL, &game->show_profiler_stats, true);
      igMenuItem_BoolPtr("Frame Time Graph", NULL, &game->show_frame_graph, true);
      igMenuItem_BoolPtr("System Telemetry", NULL, &game->show_telemetry, true);
      igMenuItem_BoolPtr("Network Profiler", NULL, &game->show_net_profiler, true);
      igSeparator();
      if (igMenuItem_Bool("Close Debug Menu", NULL, false, true)) {
        game->show_debug_menu = false;
      }
      igEndMenu();
    }
    igEndMainMenuBar();
  }

  if (game->show_profiler_stats) {
    ProfilerRenderer_DrawStatsTable();
  }
  if (game->show_frame_graph) {
    ProfilerRenderer_DrawFrameGraph();
  }
  if (game->show_telemetry) {
    Telemetry_DrawWindow();
  }
  if (game->show_net_profiler) {
    NetProfiler_DrawWindow();
  }

  rligEnd();
}

static void game_draw_debug_ui_pass(void *ctx) {
  Game *game = (Game *)ctx;
  game_draw_debug_ui(game);
}

bool Game_Init(Game *game, int64_t seed, int render_distance, const char *connect_host,
               uint16_t connect_port) {
  NetEndpoint *server_endpoint = NULL;

  *game = (Game){0};
  game->seed = seed;
  game->network_tick_rate = (int)GAME_TICK_RATE;
  game->network_tick_counter = 1;
  game->network_sequence = 1;
  game->gameplay_mode = GAMEPLAY_MODE_CREATIVE;
  game->remote_mode = (connect_host != NULL && connect_host[0] != '\0');
  game->cursor_locked = true;
  game->pending_lock_request = true;

  game->clouds_enabled = true;
  game->lock_before_escape_menu = true;

  Profiler_Init();
  game->profiler_initialized = true;
  Profiler_SetEnabled(true);

  ProfilerRenderer_Init();
  game->profiler_renderer_initialized = true;
  Telemetry_Init();
  game->telemetry_initialized = true;
  NetProfiler_Init();

  rligSetup(true);
  game->imgui_initialized = true;

  ImGuiIO *io = igGetIO_Nil();
  io->ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

  if (!Renderer_Init(&game->renderer)) {
    Game_Shutdown(game);
    return false;
  }
  game->renderer_initialized = true;
  Renderer_SetPixelateEnabled(&game->renderer, game->pixel_look_enabled);
  Mesher_SetGeometryPixelSnapEnabled(game->pixel_look_enabled);

  game->terrain_texture = LoadTexture("assets/atlas.png");
  if (game->terrain_texture.id == 0) {
    Game_Shutdown(game);
    return false;
  }

  /* 16 is unscii-16's native pixel grid; UI code picks its own on-screen
   * fontSize per draw call, and raylib scales from this base size. */
  game->font = FontAtlas_Load("assets/fonts/default.ttf", 16);
  if (game->font.texture.id == 0) {
    Game_Shutdown(game);
    return false;
  }
  SetTextureFilter(game->font.texture, TEXTURE_FILTER_POINT);
  SetTextureFilter(game->terrain_texture, TEXTURE_FILTER_POINT);

  uint32_t clay_memory_size = Clay_MinMemorySize();
  game->clay_memory = malloc(clay_memory_size);
  if (game->clay_memory == NULL) {
    Game_Shutdown(game);
    return false;
  }
  Clay_Arena clay_arena =
      Clay_CreateArenaWithCapacityAndMemory(clay_memory_size, game->clay_memory);
  Clay_Initialize(clay_arena, (Clay_Dimensions){(float)GetScreenWidth(), (float)GetScreenHeight()},
                  (Clay_ErrorHandler){0});
  Clay_SetMeasureTextFunction(ClayRender_MeasureText, &game->font);
  game->clay_initialized = true;

  if (game->remote_mode) {
    uint16_t port = (connect_port == 0) ? 25565u : connect_port;
    game->net = Net_Connect(connect_host, port);
    game->owns_net = true;
    if (game->net == NULL) {
      Game_Shutdown(game);
      return false;
    }
    game->net_initialized = true;
  } else {
    ServerConfig server_config = {
        .seed = seed,
        .render_distance = render_distance,
        .tick_rate = (int)GAME_TICK_RATE,
        .max_clients = 1,
        .port = 0,
        .mode = SERVER_MODE_INTERNAL,
    };
    strncpy(server_config.bind, "*", sizeof(server_config.bind) - 1);

    if (!Net_CreateLocalPair(&game->net, &server_endpoint)) {
      Game_Shutdown(game);
      return false;
    }
    game->net_initialized = true;
    game->owns_net = true;

    if (!ServerCore_Init(&game->internal_server, &server_config, server_endpoint, true)) {
      Game_Shutdown(game);
      return false;
    }
    game->internal_server_initialized = true;

    if (!ServerCore_StartThread(&game->internal_server)) {
      Game_Shutdown(game);
      return false;
    }
  }

  World_InitReplicated(&game->world, render_distance, game->terrain_texture);
  game->world_initialized = true;

  int spawn_y = World_GetTopY(&game->world, 0, 0);
  Vector3 spawn = {0.5f, (float)spawn_y + 2.0f, 0.5f};
  Player_Init(&game->player, spawn);
  game->player.gameplay_mode = game->gameplay_mode;
  game->player.fly_enabled = game->fly_enabled;
  game_reset_break_visual(game);
  game->view_yaw = game->player.yaw;
  game->view_pitch = game->player.pitch;

  game->camera = (Camera3D){
      .position = Player_GetEyePosition(&game->player),
      .target = (Vector3){0.0f, 0.0f, 1.0f},
      .up = (Vector3){0.0f, 1.0f, 0.0f},
      .fovy = 70.0f,
      .projection = CAMERA_PERSPECTIVE,
  };

  sync_camera(game);
  set_cursor_locked(game, true);
  game->pending_lock_request = true;
  game->clouds = (Clouds){
      .scroll_x = 0.0f,
      .speed_x = -0.9f, /* Westward drift */
      .cell_size = 8.0f,
      .layer_y = 108.5f,
      .radius_cells = 26,
      .noise_size = 256,
      .block_px = 8,
      .grid_size = 0,
      .cloud_opacity = 0.7f,
      .cell_map = NULL,
  };
  Clouds_Init(&game->clouds);
  return true;
}

void Game_Shutdown(Game *game) {
  if (game->internal_server_initialized) {
    ServerCore_Stop(&game->internal_server);
    ServerCore_Shutdown(&game->internal_server);
    game->internal_server_initialized = false;
  }

  if (game->net_initialized && game->net != NULL) {
    Net_Close(game->net);
    if (game->owns_net) {
      Net_Destroy(game->net);
    }
    game->net = NULL;
    game->net_initialized = false;
    game->owns_net = false;
  }

  if (game->world_initialized) {
    World_Shutdown(&game->world);
    game->world_initialized = false;
  }

  Clouds_Shutdown(&game->clouds);

  if (game->font.texture.id != 0) {
    UnloadFont(game->font);
    game->font = (Font){0};
  }

  if (game->clay_initialized) {
    free(game->clay_memory);
    game->clay_memory = NULL;
    game->clay_initialized = false;
  }

  if (game->renderer_initialized) {
    Renderer_Shutdown(&game->renderer);
    game->renderer_initialized = false;
  }

  if (game->terrain_texture.id != 0) {
    UnloadTexture(game->terrain_texture);
    game->terrain_texture = (Texture2D){0};
  }

  if (game->profiler_renderer_initialized) {
    ProfilerRenderer_Shutdown();
    game->profiler_renderer_initialized = false;
  }

  if (game->profiler_initialized) {
    Profiler_Shutdown();
    game->profiler_initialized = false;
  }

  if (game->telemetry_initialized) {
    Telemetry_Shutdown();
    game->telemetry_initialized = false;
  }
  NetProfiler_Shutdown();

  if (game->imgui_initialized) {
    rligShutdown();
    game->imgui_initialized = false;
  }
}

void Game_Tick(Game *game, const GameInputSnapshot *input, float tick_dt) {
  if (game->quit_requested) {
    return;
  }

  Profiler_BeginSection("Update");

  if (input->debug_menu_pressed) {
    game->show_debug_menu = !game->show_debug_menu;
  }

  if (input->escape_pressed) {
    if (game->show_debug_menu) {
      game->show_debug_menu = false;
    } else if (game->show_options) {
      game->show_options = false;
      game->show_escape_menu = true;
    } else if (game->show_escape_menu) {
      close_escape_menu(game);
    } else {
      open_escape_menu(game);
    }
  }

  if (input->cursor_lock_toggle_pressed) {
    game->pending_lock_request = false;
    set_cursor_locked(game, !game->cursor_locked);
  }

  if (input->mode_toggle_pressed) {
    if (game->gameplay_mode == GAMEPLAY_MODE_CREATIVE) {
      game->gameplay_mode = GAMEPLAY_MODE_SURVIVAL;
      game->fly_enabled = false;
    } else {
      game->gameplay_mode = GAMEPLAY_MODE_CREATIVE;
    }
    game_reset_break_visual(game);
  }

  if (input->fly_toggle_pressed && game->gameplay_mode == GAMEPLAY_MODE_CREATIVE &&
      !game_is_paused(game) && game->cursor_locked) {
    game->fly_enabled = !game->fly_enabled;
  }
  if (game->gameplay_mode == GAMEPLAY_MODE_SURVIVAL) {
    game->fly_enabled = false;
  }
  game->player.gameplay_mode = game->gameplay_mode;
  game->player.fly_enabled = game->fly_enabled;
  if (game->gameplay_mode != GAMEPLAY_MODE_SURVIVAL) {
    game_reset_break_visual(game);
  } else if (!game_is_paused(game) && game->cursor_locked && IsWindowFocused() &&
             input->left_click_held) {
    VoxelRaycastHit hit = {0};
    if (game_raycast_target_block(game, &hit)) {
      game_update_break_visual(game, &hit, tick_dt);
    } else {
      game_reset_break_visual(game);
    }
  } else {
    game_decay_break_visual(game);
  }

  if (!game_is_paused(game) && game->cursor_locked && !IsWindowFocused()) {
    game->pending_lock_request = true;
  }

  if (game->pending_lock_request && !game_is_paused(game)) {
    if (IsWindowFocused()) {
      set_cursor_locked(game, true);
      game->pending_lock_request = false;
    }
  }

  float hotbar_scroll =
      (!game_is_paused(game) && game->cursor_locked) ? input->mouse_wheel_delta : 0.0f;
  GameplayInputCmd gameplay_cmd = {0};
  NetPlayerMove player_move = {0};
  bool force_break_pulse = false;
  bool gameplay_enabled = (!game_is_paused(game) && game->cursor_locked);
  Player_ApplyHotbarScroll(&game->player, hotbar_scroll);

  Profiler_BeginSection("Network");
  game_process_network(game);

  Game_BuildGameplayInputCmd(input, game->network_tick_counter, game->player.selected_block,
                             game->gameplay_mode, game->fly_enabled, gameplay_enabled,
                             &gameplay_cmd);

  if (game->gameplay_mode == GAMEPLAY_MODE_SURVIVAL && gameplay_enabled && input->left_click_held) {
    uint32_t ticks_since_pulse = game->network_tick_counter - game->last_survival_break_send_tick;
    if (game->last_survival_break_send_tick == 0u ||
        ticks_since_pulse >= GAME_SURVIVAL_BREAK_PULSE_TICKS) {
      gameplay_cmd.buttons |= GAMEPLAY_INPUT_LEFT_CLICK;
      force_break_pulse = true;
    }
  } else if (game->gameplay_mode != GAMEPLAY_MODE_SURVIVAL || !input->left_click_held) {
    game->last_survival_break_send_tick = 0u;
  }

  if (game->welcome_received) {
    bool should_send_input;
    bool should_send_move;
    game_store_input_cmd(game, &gameplay_cmd);
    should_send_input = force_break_pulse || game_should_send_input(game, &gameplay_cmd);
    if (should_send_input && game_send_input(game, &gameplay_cmd)) {
      game_mark_input_sent(game, &gameplay_cmd);
      if (force_break_pulse) {
        game->last_survival_break_send_tick = gameplay_cmd.tick_id;
      }
    }
    game_predict_local_player(game, &gameplay_cmd, tick_dt);

    game_build_player_move(game, gameplay_cmd.tick_id, &player_move);
    should_send_move = game_should_send_player_move(game, &player_move);
    if (should_send_move && game_send_player_move(game, &player_move)) {
      game_mark_player_move_sent(game, &player_move);
    }
  }

  game->network_tick_counter++;
  Profiler_EndSection();

  Profiler_BeginSection("World");
  World_Update(&game->world, game->player.position, tick_dt);
  Profiler_EndSection();

  Clouds_Update(&game->clouds, tick_dt);

  Profiler_EndSection();
}

void Game_ApplyFrameLook(Game *game, GameInputSnapshot *frame_input) {
  float dx;
  float dy;

  if (game_is_paused(game) || !game->cursor_locked || !IsWindowFocused()) {
    frame_input->mouse_delta = (Vector2){0};
    return;
  }

  if (game->suppress_next_frame_look) {
    game->suppress_next_frame_look = false;
    frame_input->mouse_delta = (Vector2){0};
    return;
  }

  dx = frame_input->mouse_delta.x;
  dy = frame_input->mouse_delta.y;

  /* Guard against occasional cursor re-lock/focus spikes. */
  if (!isfinite(dx) || !isfinite(dy) || fabsf(dx) > GAME_LOOK_SPIKE_LIMIT ||
      fabsf(dy) > GAME_LOOK_SPIKE_LIMIT) {
    frame_input->mouse_delta = (Vector2){0};
    return;
  }

  game->view_yaw -= dx * PLAYER_MOUSE_SENSITIVITY;
  game->view_pitch -= dy * PLAYER_MOUSE_SENSITIVITY;
  game->view_pitch = Clamp(game->view_pitch, -PLAYER_PITCH_LIMIT, PLAYER_PITCH_LIMIT);
}

void Game_Draw(Game *game, float alpha) {
  Profiler_BeginSection("Render");

  int snap_downscale = (game->pixel_look_enabled && game->renderer.pixelate_downscale > 1)
                           ? game->renderer.pixelate_downscale
                           : 1;
  float snap_width = (float)((GetScreenWidth() + snap_downscale - 1) / snap_downscale);
  float snap_height = (float)((GetScreenHeight() + snap_downscale - 1) / snap_downscale);
  Mesher_SetGeometryPixelSnapResolution(snap_width, snap_height);

  sync_camera_interpolated(game, alpha);

  float ambient = World_GetAmbientMultiplier(&game->world);
  Color sky_color = {
      (unsigned char)(95.0f * ambient + 25.0f),
      (unsigned char)(140.0f * ambient + 20.0f),
      (unsigned char)(210.0f * ambient + 15.0f),
      255,
  };

  GameWorldPassContext world_pass = {
      .game = game,
      .ambient = ambient,
  };

  RendererFrameCallbacks callbacks = {
      .draw_world = game_draw_world_pass,
      .world_ctx = &world_pass,
      .draw_hud = game_draw_hud_pass,
      .hud_ctx = game,
      .draw_ui = game_draw_debug_ui_pass,
      .ui_ctx = game,
  };

  Renderer_DrawFrame(&game->renderer, &game->camera, sky_color, &callbacks);

  Profiler_EndSection();
}

static bool menu_item_clicked(Rectangle rect) {
  return CheckCollisionPointRec(GetMousePosition(), rect) &&
         IsMouseButtonReleased(MOUSE_LEFT_BUTTON);
}

static void draw_text_centered(Font font, const char *text, Rectangle area, float size) {
  Vector2 text_size = MeasureTextEx(font, text, size, 1.0f);
  Vector2 position = {area.x + (area.width - text_size.x) * 0.5f,
                      area.y + (area.height - text_size.y) * 0.5f};
  DrawTextEx(font, text, position, size, 1.0f, WHITE);
}

static bool draw_menu_button(Font font, const char *label, Rectangle rect, float scale) {
  DrawRectangleRec(rect, (Color){70, 70, 70, 220});
  draw_text_centered(font, label, rect, 18.0f * scale);
  return menu_item_clicked(rect);
}

static bool draw_menu_checkbox(Font font, const char *label, Rectangle box, bool checked,
                               float scale) {
  float font_size = 18.0f * scale;
  DrawRectangleRec(box, checked ? (Color){80, 200, 80, 255} : (Color){180, 180, 180, 255});
  DrawRectangleLinesEx(box, 2.0f * scale, WHITE);
  DrawTextEx(font, label,
             (Vector2){box.x + box.width + 10.0f * scale, box.y + (box.height - font_size) * 0.5f},
             font_size, 1.0f, WHITE);
  return menu_item_clicked(box);
}

static void draw_pause_menu(Game *game) {
  int screen_w = GetScreenWidth();
  int screen_h = GetScreenHeight();
  float scale = fmaxf((float)screen_w / 1280.0f, 1.0f);

  float panel_w = 240.0f * scale;
  float panel_h = (game->show_options ? 268.0f : 180.0f) * scale;
  float panel_x = (screen_w - panel_w) * 0.5f;
  float panel_y = (screen_h - panel_h) * 0.5f;
  DrawRectangle((int)panel_x, (int)panel_y, (int)panel_w, (int)panel_h, (Color){40, 40, 40, 230});

  float title_size = 22.0f * scale;
  float button_w = 200.0f * scale;
  float button_h = 32.0f * scale;
  float checkbox_size = 18.0f * scale;
  float x = panel_x + (panel_w - button_w) * 0.5f;
  float y = panel_y + 20.0f * scale;

  Rectangle title_area = {panel_x, y, panel_w, title_size};
  draw_text_centered(game->font, game->show_options ? "Options" : "Game Paused", title_area,
                     title_size);

  if (game->show_options) {
    y += title_size + 16.0f * scale;
    if (draw_menu_checkbox(game->font, "Clouds", (Rectangle){x, y, checkbox_size, checkbox_size},
                           game->clouds_enabled, scale)) {
      game->clouds_enabled = !game->clouds_enabled;
    }

    y += checkbox_size + 20.0f * scale;
    if (draw_menu_checkbox(game->font, "Pixel Look",
                           (Rectangle){x, y, checkbox_size, checkbox_size},
                           game->pixel_look_enabled, scale)) {
      game->pixel_look_enabled = !game->pixel_look_enabled;
      Renderer_SetPixelateEnabled(&game->renderer, game->pixel_look_enabled);
      Mesher_SetGeometryPixelSnapEnabled(game->pixel_look_enabled);
    }

    y += checkbox_size + 20.0f * scale;
    if (draw_menu_button(game->font, "Back", (Rectangle){x, y, button_w, button_h}, scale)) {
      game->show_options = false;
      game->show_escape_menu = true;
    }
    return;
  }

  y += title_size + 20.0f * scale;
  if (draw_menu_button(game->font, "Options", (Rectangle){x, y, button_w, button_h}, scale)) {
    game->show_escape_menu = false;
    game->show_options = true;
  }

  y += button_h + 8.0f * scale;
  if (draw_menu_button(game->font, "Quit", (Rectangle){x, y, button_w, button_h}, scale)) {
    game->quit_requested = true;
  }
}

void Game_DrawHUD(Game *game) {
  float screen_w = (float)GetScreenWidth();
  float screen_h = (float)GetScreenHeight();

  Clay_SetLayoutDimensions((Clay_Dimensions){screen_w, screen_h});
  Clay_SetPointerState((Clay_Vector2){(float)GetMouseX(), (float)GetMouseY()},
                       IsMouseButtonDown(MOUSE_BUTTON_LEFT));
  Clay_BeginLayout();
  CLAY({
      .id = CLAY_ID("hud_root"),
      .layout =
          {
              .sizing = {.width = CLAY_SIZING_FIXED(screen_w),
                         .height = CLAY_SIZING_FIXED(screen_h)},
              .layoutDirection = CLAY_TOP_TO_BOTTOM,
          },
  }) {
    HUD_BuildInfoPanel(&game->player, &game->world);
    HUD_BuildHotbar(&game->player);
  }
  Clay_RenderCommandArray hud_commands = Clay_EndLayout();
  ClayRender_Draw(hud_commands, &game->font);
  HUD_DrawHotbar(game->terrain_texture, &game->player);

  if (game_is_paused(game)) {
    draw_pause_menu(game);
  }

  if (game->cursor_locked && !game_is_paused(game)) {
    int cx = GetScreenWidth() / 2;
    int cy = GetScreenHeight() / 2;

    DrawLine(cx - 8, cy, cx + 8, cy, (Color){0, 0, 0, 180});
    DrawLine(cx, cy - 8, cx, cy + 8, (Color){0, 0, 0, 180});
    DrawLine(cx - 7, cy, cx + 7, cy, RAYWHITE);
    DrawLine(cx, cy - 7, cx, cy + 7, RAYWHITE);
  }
}
