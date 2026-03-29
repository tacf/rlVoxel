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
#include "ui/hud.h"
#include "ui/ui.h"
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "world/blocks.h"
#include "world/world.h"
#include "profiling/profiler.h"
#include "profiling/profiler_renderer.h"
#include "diagnostics/net_profiler.h"
#include "diagnostics/telemetry.h"

/* Keep first-person look response exactly tied to local mouse deltas. */
#define GAME_VIEW_PITCH_LIMIT 1.55f
#define GAME_VIEW_MOUSE_SENSITIVITY 0.0023f
#define GAME_LOOK_SPIKE_LIMIT 180.0f

/*
 * Reconciliation tuning.
 * Rebuild only when server/prediction drift is meaningful.
 */
#define GAME_RECONCILE_SNAP_DISTANCE 2.00f
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
#define GAME_BREAK_DAMAGE_PER_HIT 1.0f
#define GAME_BREAK_RECOVERY_DELAY_TICKS 10u
#define GAME_BREAK_RECOVERY_INTERVAL_TICKS 5u
#define GAME_BREAK_ACK_TIMEOUT_TICKS 20u

/* Build raylib camera vectors from a player pose (position + yaw/pitch). */
static void set_camera_from_pose(Game *game, Vector3 position, float yaw, float pitch) {
  Vector3 eye = {position.x, position.y + PLAYER_EYE_HEIGHT, position.z};
  float cos_pitch = cosf(pitch);
  Vector3 dir = Vector3Normalize((Vector3){
      sinf(yaw) * cos_pitch,
      sinf(pitch),
      cosf(yaw) * cos_pitch,
  });

  game->camera.position = eye;
  game->camera.target = Vector3Add(eye, dir);
  game->camera.up = (Vector3){0.0f, 1.0f, 0.0f};
}

/* Snap camera directly to the player's current simulation pose. */
static void sync_camera(Game *game) {
  float yaw = game->view_initialized ? game->view_yaw : game->player.yaw;
  float pitch = game->view_initialized ? game->view_pitch : game->player.pitch;
  set_camera_from_pose(game, game->player.position, yaw, pitch);
}

/* Interpolate position only; keep look immediate from local view yaw/pitch. */
static void sync_camera_interpolated(Game *game, float alpha) {
  Vector3 position;
  float yaw;
  float pitch;

  if (game == NULL) {
    return;
  }

  alpha = Clamp(alpha, 0.0f, 1.0f);
  position = Vector3Lerp(game->player.previous_position, game->player.position, alpha);
  yaw = game->view_initialized ? game->view_yaw : game->player.yaw;
  pitch = game->view_initialized ? game->view_pitch : game->player.pitch;
  set_camera_from_pose(game, position, yaw, pitch);
}

static void game_reset_break_visual(Game *game) {
  if (game == NULL) {
    return;
  }
  game->break_visual_active = false;
  game->break_visual_x = 0;
  game->break_visual_y = 0;
  game->break_visual_z = 0;
  game->break_visual_damage = 0.0f;
  game->break_visual_last_tick = 0u;
  game->break_visual_last_recover_tick = 0u;
}

static void game_decay_break_visual(Game *game) {
  uint32_t recovery_start_tick;
  uint32_t elapsed_recovery_ticks;
  uint32_t recovery_steps;
  if (game == NULL || !game->break_visual_active) {
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
  game->break_visual_damage -= (float)recovery_steps * GAME_BREAK_DAMAGE_PER_HIT;
  game->break_visual_last_recover_tick += recovery_steps * GAME_BREAK_RECOVERY_INTERVAL_TICKS;
  if (game->break_visual_damage <= 0.0f) {
    game_reset_break_visual(game);
  }
}

static PendingBreakOp *game_find_pending_break(Game *game, int x, int y, int z) {
  size_t i;

  if (game == NULL) {
    return NULL;
  }

  for (i = 0; i < GAME_PENDING_BREAK_OPS_MAX; i++) {
    PendingBreakOp *op = &game->pending_break_ops[i];
    if (!op->active) {
      continue;
    }
    if (op->x == x && op->y == y && op->z == z) {
      return op;
    }
  }

  return NULL;
}

static PendingBreakOp *game_alloc_pending_break(Game *game) {
  size_t i;

  if (game == NULL) {
    return NULL;
  }

  for (i = 0; i < GAME_PENDING_BREAK_OPS_MAX; i++) {
    PendingBreakOp *op = &game->pending_break_ops[i];
    if (!op->active) {
      return op;
    }
  }

  /*
   * Saturation fallback: reuse the oldest tracked entry so optimistic
   * confirmation keeps moving forward under bursty local edits.
   */
  {
    PendingBreakOp *oldest = &game->pending_break_ops[0];
    for (i = 1; i < GAME_PENDING_BREAK_OPS_MAX; i++) {
      PendingBreakOp *candidate = &game->pending_break_ops[i];
      if (candidate->sent_tick < oldest->sent_tick) {
        oldest = candidate;
      }
    }
    return oldest;
  }
}

static void game_clear_pending_break(PendingBreakOp *op) {
  if (op == NULL) {
    return;
  }
  memset(op, 0, sizeof(*op));
}

static void game_track_optimistic_break(Game *game, int x, int y, int z, uint32_t sent_tick) {
  PendingBreakOp *op;
  uint8_t previous_block_id;
  int previous_skylight;
  bool applied;

  if (game == NULL) {
    return;
  }

  previous_block_id = World_GetBlock(&game->world, x, y, z);
  if (previous_block_id == BLOCK_AIR || previous_block_id == BLOCK_BEDROCK) {
    return;
  }

  previous_skylight = World_GetSkyLight(&game->world, x, y, z);
  if (previous_skylight < 0) {
    previous_skylight = 0;
  } else if (previous_skylight > 15) {
    previous_skylight = 15;
  }

  applied = World_ApplyBlockDelta(&game->world, x, y, z, BLOCK_AIR, (uint8_t)previous_skylight);
  if (!applied) {
    return;
  }

  op = game_find_pending_break(game, x, y, z);
  if (op == NULL) {
    op = game_alloc_pending_break(game);
  }
  if (op == NULL) {
    return;
  }

  *op = (PendingBreakOp){
      .x = x,
      .y = y,
      .z = z,
      .previous_block_id = previous_block_id,
      .previous_skylight = (uint8_t)previous_skylight,
      .sent_tick = sent_tick,
      .active = true,
  };
}

static void game_ack_pending_break(Game *game, int x, int y, int z) {
  PendingBreakOp *op;

  if (game == NULL) {
    return;
  }

  op = game_find_pending_break(game, x, y, z);
  if (op != NULL) {
    game_clear_pending_break(op);
  }
}

static void game_update_pending_break_timeouts(Game *game) {
  size_t i;

  if (game == NULL) {
    return;
  }

  for (i = 0; i < GAME_PENDING_BREAK_OPS_MAX; i++) {
    PendingBreakOp *op = &game->pending_break_ops[i];
    uint32_t elapsed;
    if (!op->active) {
      continue;
    }

    elapsed = game->network_tick_counter - op->sent_tick;
    if (elapsed < GAME_BREAK_ACK_TIMEOUT_TICKS) {
      continue;
    }

    World_ApplyBlockDelta(&game->world, op->x, op->y, op->z, op->previous_block_id,
                          op->previous_skylight);
    game_clear_pending_break(op);
  }
}

static void game_clear_pending_breaks(Game *game) {
  if (game == NULL) {
    return;
  }
  memset(game->pending_break_ops, 0, sizeof(game->pending_break_ops));
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

static void open_debug_menu(Game *game) {
  if (game->show_debug_menu) {
    return;
  }

  game->show_debug_menu = true;
}

static void close_debug_menu(Game *game) {
  if (!game->show_debug_menu) {
    return;
  }

  game->show_debug_menu = false;
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

static bool game_send_hello(Game *game, int requested_render_distance) {
  NetHello hello = {
      .requested_render_distance = (requested_render_distance > 0)
                                       ? (uint32_t)requested_render_distance
                                       : (uint32_t)DEFAULT_RENDER_DISTANCE,
  };
  uint32_t sequence;
  bool sent;

  if (game == NULL || game->net == NULL) {
    return false;
  }

  sequence = game->network_sequence++;
  sent = Net_SendHello(game->net, sequence, game->network_tick_counter, &hello);
  if (sent) {
    size_t packet_size = Protocol_FixedPacketSize(NET_MSG_C2S_HELLO);
    if (packet_size == 0u) {
      packet_size = Protocol_HeaderSize();
    }
    NetProfiler_RecordSend(NET_MSG_C2S_HELLO, sequence, game->network_tick_counter,
                           packet_size, 0u, 0u);
  }

  return sent;
}

static bool game_send_input(Game *game, const GameplayInputCmd *cmd) {
  uint32_t sequence;
  bool sent;

  if (game == NULL || cmd == NULL || game->net == NULL || !game->connected ||
      !game->welcome_received) {
    return false;
  }

  sequence = game->network_sequence++;
  sent = Net_SendInputCmd(game->net, sequence, game->network_tick_counter, cmd);
  if (sent) {
    size_t packet_size = Protocol_FixedPacketSize(NET_MSG_C2S_INPUT_CMD);
    if (packet_size == 0u) {
      packet_size = Protocol_HeaderSize();
    }
    NetProfiler_RecordSend(NET_MSG_C2S_INPUT_CMD, sequence, game->network_tick_counter,
                           packet_size, 1u, cmd->tick_id);
  }

  return sent;
}

static bool game_send_player_move(Game *game, const NetPlayerMove *move) {
  uint32_t sequence;
  bool sent;

  if (game == NULL || move == NULL || game->net == NULL || !game->connected ||
      !game->welcome_received) {
    return false;
  }

  sequence = game->network_sequence++;
  sent = Net_SendPlayerMove(game->net, sequence, game->network_tick_counter, move);
  if (sent) {
    size_t packet_size = Protocol_FixedPacketSize(NET_MSG_C2S_PLAYER_MOVE);
    if (packet_size == 0u) {
      packet_size = Protocol_HeaderSize();
    }
    NetProfiler_RecordSend(NET_MSG_C2S_PLAYER_MOVE, sequence, game->network_tick_counter,
                           packet_size, 1u, move->tick_id);
  }

  return sent;
}

static bool game_input_cmd_semantic_equal(const GameplayInputCmd *a, const GameplayInputCmd *b) {
  uint8_t a_action_buttons;
  uint8_t b_action_buttons;

  if (a == NULL || b == NULL) {
    return false;
  }

  a_action_buttons = a->buttons & (uint8_t)(GAMEPLAY_INPUT_LEFT_CLICK | GAMEPLAY_INPUT_RIGHT_CLICK);
  b_action_buttons = b->buttons & (uint8_t)(GAMEPLAY_INPUT_LEFT_CLICK | GAMEPLAY_INPUT_RIGHT_CLICK);
  return a_action_buttons == b_action_buttons && a->selected_block == b->selected_block &&
         a->gameplay_mode == b->gameplay_mode && a->fly_enabled == b->fly_enabled;
}

static bool game_should_send_input(const Game *game, const GameplayInputCmd *cmd) {
  uint32_t ticks_since_last_send;

  if (game == NULL || cmd == NULL) {
    return false;
  }

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
  if (game == NULL || cmd == NULL) {
    return;
  }

  game->last_sent_input_cmd = *cmd;
  game->last_sent_input_tick_id = cmd->tick_id;
  game->has_last_sent_input_cmd = true;
}

static void game_build_player_move(const Game *game, uint32_t tick_id, NetPlayerMove *out_move) {
  if (game == NULL || out_move == NULL) {
    return;
  }

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
  if (a == NULL || b == NULL) {
    return false;
  }

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

  if (game == NULL || move == NULL) {
    return false;
  }

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
  if (game == NULL || move == NULL) {
    return;
  }

  game->last_sent_move = *move;
  game->last_sent_move_tick_id = move->tick_id;
  game->has_last_sent_move = true;
}

static float game_network_tick_dt(const Game *game) {
  int rate;
  if (game == NULL) {
    return 1.0f / (float)GAME_TICK_RATE;
  }

  rate = (game->network_tick_rate > 0) ? game->network_tick_rate : (int)GAME_TICK_RATE;
  return 1.0f / (float)rate;
}

static void game_reset_prediction_history(Game *game) {
  if (game == NULL) {
    return;
  }
  memset(game->prediction_history, 0, sizeof(game->prediction_history));
  memset(game->input_history, 0, sizeof(game->input_history));
}

static PredictedPlayerSample *game_prediction_slot(Game *game, uint32_t tick_id) {
  if (game == NULL) {
    return NULL;
  }
  return &game->prediction_history[tick_id % GAME_PREDICTION_HISTORY_SIZE];
}

static const PredictedPlayerSample *game_prediction_find(const Game *game, uint32_t tick_id) {
  const PredictedPlayerSample *slot;
  if (game == NULL) {
    return NULL;
  }

  slot = &game->prediction_history[tick_id % GAME_PREDICTION_HISTORY_SIZE];
  if (!slot->valid || slot->tick_id != tick_id) {
    return NULL;
  }

  return slot;
}

static GameplayInputHistoryEntry *game_input_slot(Game *game, uint32_t tick_id) {
  if (game == NULL) {
    return NULL;
  }

  return &game->input_history[tick_id % GAME_INPUT_HISTORY_SIZE];
}

static void game_store_input_cmd(Game *game, const GameplayInputCmd *cmd) {
  GameplayInputHistoryEntry *slot;

  if (game == NULL || cmd == NULL) {
    return;
  }

  slot = game_input_slot(game, cmd->tick_id);
  if (slot == NULL) {
    return;
  }

  slot->tick_id = cmd->tick_id;
  slot->cmd = *cmd;
  slot->valid = true;
}

static const GameplayInputCmd *game_find_input_cmd(const Game *game, uint32_t tick_id) {
  const GameplayInputHistoryEntry *slot;

  if (game == NULL) {
    return NULL;
  }

  slot = &game->input_history[tick_id % GAME_INPUT_HISTORY_SIZE];
  if (!slot->valid || slot->tick_id != tick_id) {
    return NULL;
  }

  return &slot->cmd;
}

static void game_store_prediction_sample(Game *game, uint32_t tick_id) {
  PredictedPlayerSample *slot = game_prediction_slot(game, tick_id);
  if (slot == NULL) {
    return;
  }

  *slot = (PredictedPlayerSample){
      .tick_id = tick_id,
      .position = game->player.position,
      .velocity = game->player.velocity,
      .on_ground = game->player.on_ground,
      .valid = true,
  };
}

static void game_apply_simulation_cmd(Game *game, const GameplayInputCmd *cmd, float tick_dt,
                                      bool store_prediction) {
  GameInputSnapshot sim = {0};

  if (game == NULL || cmd == NULL || tick_dt <= 0.0f) {
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
  if (store_prediction) {
    game_store_prediction_sample(game, cmd->tick_id);
  }
}

static void game_predict_local_player(Game *game, const GameplayInputCmd *cmd, float tick_dt) {
  if (game == NULL || cmd == NULL || !game->connected || !game->welcome_received) {
    return;
  }

  game_apply_simulation_cmd(game, cmd, tick_dt, true);
}

static void game_apply_authoritative_state(Game *game, const AuthoritativePlayerState *state,
                                           bool sync_orientation) {
  if (game == NULL || state == NULL) {
    return;
  }

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

  if (!game->view_initialized) {
    game->view_yaw = state->yaw;
    game->view_pitch = state->pitch;
    game->view_initialized = true;
  }

  World_SetTime(&game->world, state->world_time);
}

static void game_replay_unacked_inputs(Game *game, uint32_t acknowledged_input_tick) {
  uint32_t replay_tick;
  uint32_t stop_tick;
  float tick_dt;

  if (game == NULL) {
    return;
  }

  tick_dt = game_network_tick_dt(game);
  replay_tick = acknowledged_input_tick + 1u;
  stop_tick = game->network_tick_counter;

  while (replay_tick < stop_tick) {
    const GameplayInputCmd *cmd = game_find_input_cmd(game, replay_tick);
    if (cmd != NULL) {
      game_apply_simulation_cmd(game, cmd, tick_dt, true);
    }
    replay_tick++;
  }
}

static void game_reconcile_player_state(Game *game, const AuthoritativePlayerState *state) {
  uint32_t acknowledged_input_tick;
  const PredictedPlayerSample *predicted;
  Vector3 authoritative_pos;
  Vector3 predicted_pos;
  Vector3 delta;
  float horizontal_error;
  float vertical_error;
  float distance_sq;

  if (game == NULL || state == NULL) {
    return;
  }

  acknowledged_input_tick = state->input_tick_id;
  if (game->has_authoritative_state) {
    if (acknowledged_input_tick < game->last_authoritative_tick) {
      return;
    }
    if (acknowledged_input_tick == game->last_authoritative_tick) {
      /*
       * Do not re-correct against the same ack repeatedly.
       * This is a common source of visible side-to-side micro jitter.
       */
      game->player.velocity = (Vector3){state->velocity_x, state->velocity_y, state->velocity_z};
      game->player.on_ground = state->on_ground != 0;
      World_SetTime(&game->world, state->world_time);
      return;
    }
  }

  authoritative_pos = (Vector3){state->position_x, state->position_y, state->position_z};
  if (!game->has_authoritative_state) {
    game_apply_authoritative_state(game, state, true);
    game_replay_unacked_inputs(game, acknowledged_input_tick);
    return;
  }

  predicted = game_prediction_find(game, acknowledged_input_tick);
  if (predicted == NULL) {
    /* Missing history fallback: authoritative reset + replay from ack. */
    game_apply_authoritative_state(game, state, false);
    game_replay_unacked_inputs(game, acknowledged_input_tick);
    return;
  }

  predicted_pos = predicted->position;
  delta = Vector3Subtract(authoritative_pos, predicted_pos);
  horizontal_error = sqrtf(delta.x * delta.x + delta.z * delta.z);
  vertical_error = fabsf(delta.y);
  distance_sq = Vector3LengthSqr(delta);

  if (horizontal_error <= GAME_RECONCILE_IGNORE_HORIZONTAL &&
      vertical_error <= GAME_RECONCILE_IGNORE_VERTICAL) {
    game->last_authoritative_tick = acknowledged_input_tick;
    game->has_authoritative_state = true;
    game->player.velocity = (Vector3){state->velocity_x, state->velocity_y, state->velocity_z};
    game->player.on_ground = state->on_ground != 0;
    World_SetTime(&game->world, state->world_time);
    return;
  }

  if (distance_sq >= (GAME_RECONCILE_SNAP_DISTANCE * GAME_RECONCILE_SNAP_DISTANCE)) {
    game_apply_authoritative_state(game, state, false);
    game_replay_unacked_inputs(game, acknowledged_input_tick);
    return;
  }

  game_apply_authoritative_state(game, state, false);
  game_replay_unacked_inputs(game, acknowledged_input_tick);
}

static void game_process_network(Game *game) {
  NetEvent event;
  AuthoritativePlayerState latest_player_state = {0};
  bool has_latest_player_state = false;

  if (game == NULL || game->net == NULL) {
    return;
  }

  Net_Update(game->net, 0);

  while (Net_PollEvent(game->net, &event)) {
    if (event.type == NET_EVENT_CONNECTED) {
      game->connected = true;
      game->has_last_sent_move = false;
      game->last_sent_move_tick_id = 0u;
      game->has_last_sent_input_cmd = false;
      game->last_sent_input_tick_id = 0u;
      game->last_survival_break_send_tick = 0u;
      game_clear_pending_breaks(game);
      game_send_hello(game, game->world.render_distance);
    } else if (event.type == NET_EVENT_DISCONNECTED) {
      game->connected = false;
      game->welcome_received = false;
      game->has_authoritative_state = false;
      game->last_authoritative_tick = 0;
      game->has_last_sent_move = false;
      game->last_sent_move_tick_id = 0u;
      game->has_last_sent_input_cmd = false;
      game->last_sent_input_tick_id = 0u;
      game->last_survival_break_send_tick = 0u;
      game_reset_prediction_history(game);
      game_clear_pending_breaks(game);
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
        game->has_authoritative_state = false;
        game->last_authoritative_tick = 0;
        game->has_last_sent_move = false;
        game->last_sent_move_tick_id = 0u;
        game->has_last_sent_input_cmd = false;
        game->last_sent_input_tick_id = 0u;
        game->last_survival_break_send_tick = 0u;
        game_reset_prediction_history(game);
        game_clear_pending_breaks(game);
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
        if (event.payload.block_delta.block_id == BLOCK_AIR) {
          game_ack_pending_break(game, event.payload.block_delta.x, event.payload.block_delta.y,
                                 event.payload.block_delta.z);
        }
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
  float yaw;
  float pitch;
  float cos_pitch;
  if (game == NULL) {
    return (Vector3){0.0f, 0.0f, 1.0f};
  }

  yaw = game->view_initialized ? game->view_yaw : game->player.yaw;
  pitch = game->view_initialized ? game->view_pitch : game->player.pitch;
  cos_pitch = cosf(pitch);
  return Vector3Normalize((Vector3){
      sinf(yaw) * cos_pitch,
      sinf(pitch),
      cosf(yaw) * cos_pitch,
  });
}

static bool game_raycast_target_block(const Game *game, VoxelRaycastHit *out_hit) {
  Vector3 eye;
  Vector3 dir;

  if (game == NULL || out_hit == NULL) {
    return false;
  }

  eye = Player_GetEyePosition(&game->player);
  dir = game_view_direction(game);
  return Raycast_VoxelForPlacement(&game->world, eye, dir, PLAYER_REACH_DISTANCE, out_hit) &&
         out_hit->hit;
}

static bool game_register_break_hit(Game *game, const VoxelRaycastHit *hit) {
  int durability;

  if (game == NULL || hit == NULL || !hit->hit) {
    return false;
  }
  if (hit->block_id == BLOCK_BEDROCK) {
    game_reset_break_visual(game);
    return false;
  }

  durability = Block_GetDurability(hit->block_id);
  if (durability <= 0) {
    game_reset_break_visual(game);
    return false;
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

  game->break_visual_damage += GAME_BREAK_DAMAGE_PER_HIT;
  game->break_visual_last_tick = game->network_tick_counter;
  if (game->break_visual_damage >= (float)durability) {
    game_reset_break_visual(game);
    return true;
  }
  return false;
}

static int game_break_visual_stage(const Game *game) {
  uint8_t block_id;
  int durability;
  float ratio;
  int stage;

  if (game == NULL || !game->break_visual_active) {
    return -1;
  }

  block_id = World_GetBlock(&game->world, game->break_visual_x, game->break_visual_y,
                            game->break_visual_z);
  durability = Block_GetDurability(block_id);
  if (durability <= 0) {
    return -1;
  }

  ratio = game->break_visual_damage / (float)durability;
  ratio = Clamp(ratio, 0.0f, 0.9999f);
  stage = (int)floorf(ratio * 10.0f);
  if (stage < 0) {
    stage = 0;
  }
  if (stage > 9) {
    stage = 9;
  }
  return stage;
}

static void game_draw_target_block_highlight(Game *game, const Camera3D *camera) {
  int break_stage;
  VoxelRaycastHit hit;
  (void)camera;

  if (game == NULL || camera == NULL || !game->cursor_locked || game_is_paused(game)) {
    return;
  }

  if (game_raycast_target_block(game, &hit)) {
    SelectionHighlight_Draw(&hit);
  }

  if (game->gameplay_mode == GAMEPLAY_MODE_SURVIVAL && game->break_visual_active) {
    break_stage = game_break_visual_stage(game);
    if (break_stage >= 0) {
      SelectionHighlight_DrawDamageOverlay(game->terrain_texture, game->break_visual_x,
                                           game->break_visual_y, game->break_visual_z,
                                           break_stage);
    }
  }
}

typedef struct GameWorldPassContext {
  Game *game;
  float ambient;
} GameWorldPassContext;

static void game_draw_world_pass(void *ctx, const Camera3D *camera) {
  GameWorldPassContext *pass = (GameWorldPassContext *)ctx;
  if (pass == NULL || pass->game == NULL) {
    return;
  }

  if (pass->game->clouds_enabled) {
    Clouds_Draw(&pass->game->clouds, camera, pass->ambient);
  }
  World_Draw(&pass->game->world, camera, pass->ambient);
  game_draw_target_block_highlight(pass->game, camera);
}

static void game_draw_hud_pass(void *ctx) {
  Game *game = (Game *)ctx;
  if (game == NULL) {
    return;
  }

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
        close_debug_menu(game);
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
  if (game == NULL) {
    return;
  }

  game_draw_debug_ui(game);
}

bool Game_Init(Game *game, int64_t seed, int render_distance, const char *connect_host,
               uint16_t connect_port) {
  NetEndpoint *server_endpoint = NULL;

  if (!game) {
    return false;
  }

  *game = (Game){0};
  game->seed = seed;
  game->network_tick_rate = (int)GAME_TICK_RATE;
  game->network_tick_counter = 1;
  game->network_sequence = 1;
  game->has_last_sent_move = false;
  game->last_sent_move_tick_id = 0u;
  game->has_last_sent_input_cmd = false;
  game->last_sent_input_tick_id = 0u;
  game->last_survival_break_send_tick = 0u;
  game->gameplay_mode = GAMEPLAY_MODE_CREATIVE;
  game->fly_enabled = false;
  game->remote_mode = (connect_host != NULL && connect_host[0] != '\0');
  game->cursor_locked = true;
  game->pending_lock_request = true;

  game->show_escape_menu = false;
  game->show_options = false;
  game->clouds_enabled = true;
  game->quit_requested = false;
  game->lock_before_escape_menu = true;

  game->show_debug_menu = false;
  game->show_profiler_stats = false;
  game->show_frame_graph = false;
  game->show_telemetry = false;
  game->show_net_profiler = false;
  game->suppress_next_frame_look = false;

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

  ImGuiIO *io = igGetIO();
  io->ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

  if (!Renderer_Init(&game->renderer)) {
    Game_Shutdown(game);
    return false;
  }
  game->renderer_initialized = true;

  if (!UI_Init(&game->ui, UI_DEFAULT_MAX_NODES, UI_DEFAULT_TEXT_CAPACITY, UI_DEFAULT_MAX_STATES)) {
    Game_Shutdown(game);
    return false;
  }
  UI_SetReferenceResolution(&game->ui, 1280.0f, 720.0f);
  game->ui_initialized = true;

  game->terrain_texture = LoadTexture("assets/atlas.png");
  if (game->terrain_texture.id == 0) {
    Game_Shutdown(game);
    return false;
  }

  game->font = LoadFontEx("assets/fonts/default.ttf", 20, NULL, 0);
  if (game->font.texture.id == 0) {
    Game_Shutdown(game);
    return false;
  }
  SetTextureFilter(game->font.texture, TEXTURE_FILTER_POINT);
  SetTextureFilter(game->terrain_texture, TEXTURE_FILTER_POINT);

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
  game->view_initialized = true;

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
  if (!game) {
    return;
  }

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

  if (game->ui_initialized) {
    UI_Shutdown(&game->ui);
    game->ui_initialized = false;
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
  if (game == NULL || input == NULL) {
    return;
  }

  if (game->quit_requested) {
    return;
  }

  Profiler_BeginSection("Update");

  if (input->debug_menu_pressed) {
    if (game->show_debug_menu) {
      close_debug_menu(game);
    } else {
      open_debug_menu(game);
    }
  }

  if (input->escape_pressed) {
    if (game->show_debug_menu) {
      close_debug_menu(game);
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
  bool optimistic_break_commit = false;
  VoxelRaycastHit optimistic_break_hit = {0};
  bool gameplay_enabled = (!game_is_paused(game) && game->cursor_locked);
  Player_ApplyHotbarScroll(&game->player, hotbar_scroll);

  Profiler_BeginSection("Network");
  game_process_network(game);
  game_update_pending_break_timeouts(game);

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

  if (force_break_pulse) {
    VoxelRaycastHit hit = {0};
    if (game_raycast_target_block(game, &hit)) {
      optimistic_break_commit = game_register_break_hit(game, &hit);
      if (optimistic_break_commit) {
        optimistic_break_hit = hit;
      }
    }
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
      if (optimistic_break_commit) {
        game_track_optimistic_break(game, optimistic_break_hit.block_x, optimistic_break_hit.block_y,
                                    optimistic_break_hit.block_z, gameplay_cmd.tick_id);
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

  if (game == NULL || frame_input == NULL) {
    return;
  }

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

  game->view_yaw -= dx * GAME_VIEW_MOUSE_SENSITIVITY;
  game->view_pitch -= dy * GAME_VIEW_MOUSE_SENSITIVITY;
  game->view_pitch = Clamp(game->view_pitch, -GAME_VIEW_PITCH_LIMIT, GAME_VIEW_PITCH_LIMIT);
  game->view_initialized = true;
}

void Game_Draw(Game *game, float alpha) {
  Profiler_BeginSection("Render");

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

void Game_DrawHUD(Game *game) {
  UI_BeginFrame(&game->ui, game->font, 20.0f);
  HUD_BuildInfoPanel(&game->ui, &game->player, &game->world);
  HUD_BuildHotbar(&game->ui, game->terrain_texture, &game->player);
  UI_EndFrame(&game->ui);

  if (game_is_paused(game)) {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float scale = (float)sw / 1280.0f;
    if (scale < 1.0f)
      scale = 1.0f;

    float panel_w = 240.0f * scale;
    float panel_h = game->show_options ? 220.0f * scale : 180.0f * scale;
    float panel_x = (sw - panel_w) * 0.5f;
    float panel_y = (sh - panel_h) * 0.5f;

    DrawRectangle((int)panel_x, (int)panel_y, (int)panel_w, (int)panel_h, (Color){40, 40, 40, 230});

    float font_title = 22.0f * scale;
    float font_btn = 18.0f * scale;
    float btn_w = 200.0f * scale;
    float btn_h = 32.0f * scale;
    float btn_x = panel_x + (panel_w - btn_w) * 0.5f;
    float cursor_y = panel_y + 20.0f * scale;

    if (game->show_options) {
      DrawTextEx(
          game->font, "Options",
          (Vector2){panel_x +
                        (panel_w - MeasureTextEx(game->font, "Options", font_title, 1.0f).x) * 0.5f,
                    cursor_y},
          font_title, 1.0f, WHITE);
      cursor_y += font_title + 16.0f * scale;

      float cb_size = 18.0f * scale;
      Rectangle cb_rect = {btn_x, cursor_y, cb_size, cb_size};
      if (game->clouds_enabled) {
        DrawRectangleRec(cb_rect, (Color){80, 200, 80, 255});
      } else {
        DrawRectangleRec(cb_rect, (Color){180, 180, 180, 255});
      }
      DrawRectangleLinesEx(cb_rect, 2.0f * scale, WHITE);
      DrawTextEx(game->font, "Clouds",
                 (Vector2){btn_x + cb_size + 10.0f * scale, cursor_y + (cb_size - font_btn) * 0.5f},
                 font_btn, 1.0f, WHITE);

      if (CheckCollisionPointRec(GetMousePosition(), cb_rect) &&
          IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        game->clouds_enabled = !game->clouds_enabled;
      }
      cursor_y += cb_size + 20.0f * scale;

      Rectangle back_rect = {btn_x, cursor_y, btn_w, btn_h};
      DrawRectangleRec(back_rect, (Color){70, 70, 70, 220});
      Vector2 bt = MeasureTextEx(game->font, "Back", font_btn, 1.0f);
      DrawTextEx(
          game->font, "Back",
          (Vector2){back_rect.x + (btn_w - bt.x) * 0.5f, back_rect.y + (btn_h - bt.y) * 0.5f},
          font_btn, 1.0f, WHITE);

      if (CheckCollisionPointRec(GetMousePosition(), back_rect) &&
          IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        game->show_options = false;
        game->show_escape_menu = true;
      }
    } else {
      DrawTextEx(
          game->font, "Game Paused",
          (Vector2){panel_x +
                        (panel_w - MeasureTextEx(game->font, "Game Paused", font_title, 1.0f).x) *
                            0.5f,
                    cursor_y},
          font_title, 1.0f, WHITE);
      cursor_y += font_title + 20.0f * scale;

      Rectangle opt_rect = {btn_x, cursor_y, btn_w, btn_h};
      DrawRectangleRec(opt_rect, (Color){70, 70, 70, 220});
      Vector2 ot = MeasureTextEx(game->font, "Options", font_btn, 1.0f);
      DrawTextEx(game->font, "Options",
                 (Vector2){opt_rect.x + (btn_w - ot.x) * 0.5f, opt_rect.y + (btn_h - ot.y) * 0.5f},
                 font_btn, 1.0f, WHITE);

      if (CheckCollisionPointRec(GetMousePosition(), opt_rect) &&
          IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        game->show_escape_menu = false;
        game->show_options = true;
      }
      cursor_y += btn_h + 8.0f * scale;

      Rectangle quit_rect = {btn_x, cursor_y, btn_w, btn_h};
      DrawRectangleRec(quit_rect, (Color){70, 70, 70, 220});
      Vector2 qt = MeasureTextEx(game->font, "Quit", font_btn, 1.0f);
      DrawTextEx(
          game->font, "Quit",
          (Vector2){quit_rect.x + (btn_w - qt.x) * 0.5f, quit_rect.y + (btn_h - qt.y) * 0.5f},
          font_btn, 1.0f, WHITE);

      if (CheckCollisionPointRec(GetMousePosition(), quit_rect) &&
          IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        game->quit_requested = true;
      }
    }
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
