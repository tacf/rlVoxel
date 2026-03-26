#include "server/server_core.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <raylib.h>
#include <raymath.h>

#include "constants.h"
#include "game/game_input.h"
#include "game/player.h"
#include "game/raycast.h"
#include "net/net.h"
#include "net/protocol.h"
#include "world/blocks.h"
#include "world/chunk.h"
#include "world/world.h"

typedef struct ServerOutgoingNode {
  ServerOutgoingMessage message;
  struct ServerOutgoingNode *next;
} ServerOutgoingNode;

static double now_seconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void sleep_millis(int ms) {
  struct timespec ts;
  if (ms <= 0) {
    return;
  }
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}

static int floor_div16(int x) {
  if (x >= 0) {
    return x >> 4;
  }
  return -(((-x) + 15) >> 4);
}

static int64_t make_chunk_key(int cx, int cz) { return ((int64_t)cx << 32) | (uint32_t)cz; }

#define SERVER_MOVE_MAX_HORIZ_PER_TICK 1.20f
#define SERVER_MOVE_MAX_VERT_PER_TICK 2.40f
#define SERVER_MOVE_MAX_TICK_GAP 20u
#define SERVER_BREAK_DAMAGE_PER_HIT 1.0f
#define SERVER_BREAK_RECOVERY_DELAY_TICKS 10u
#define SERVER_BREAK_RECOVERY_INTERVAL_TICKS 5u

static void server_reset_break_state(ServerCore *server) {
  if (server == NULL) {
    return;
  }
  server->break_state_active = false;
  server->break_block_x = 0;
  server->break_block_y = 0;
  server->break_block_z = 0;
  server->break_damage = 0.0f;
  server->break_last_tick = 0u;
  server->break_last_recover_tick = 0u;
}

static void server_decay_break_state(ServerCore *server) {
  uint32_t recovery_start_tick;
  uint32_t elapsed_recovery_ticks;
  uint32_t recovery_steps;

  if (server == NULL || !server->break_state_active) {
    return;
  }

  recovery_start_tick = server->break_last_tick + SERVER_BREAK_RECOVERY_DELAY_TICKS;
  if (server->tick_counter < recovery_start_tick) {
    return;
  }

  if (server->break_last_recover_tick < recovery_start_tick) {
    server->break_last_recover_tick = recovery_start_tick;
  }

  if (server->tick_counter < server->break_last_recover_tick + SERVER_BREAK_RECOVERY_INTERVAL_TICKS) {
    return;
  }

  elapsed_recovery_ticks = server->tick_counter - server->break_last_recover_tick;
  recovery_steps = elapsed_recovery_ticks / SERVER_BREAK_RECOVERY_INTERVAL_TICKS;
  server->break_damage -= (float)recovery_steps * SERVER_BREAK_DAMAGE_PER_HIT;
  server->break_last_recover_tick += recovery_steps * SERVER_BREAK_RECOVERY_INTERVAL_TICKS;
  if (server->break_damage <= 0.0f) {
    server_reset_break_state(server);
  }
}

static bool sent_chunk_contains(const ServerCore *server, int64_t key) {
  for (size_t i = 0; i < server->sent_chunk_count; i++) {
    if (server->sent_chunk_keys[i] == key) {
      return true;
    }
  }
  return false;
}

static bool sent_chunk_add(ServerCore *server, int64_t key) {
  if (sent_chunk_contains(server, key)) {
    return true;
  }

  if (server->sent_chunk_count == server->sent_chunk_capacity) {
    size_t new_cap = (server->sent_chunk_capacity == 0) ? 64 : server->sent_chunk_capacity * 2;
    int64_t *new_keys = (int64_t *)realloc(server->sent_chunk_keys, new_cap * sizeof(int64_t));
    if (new_keys == NULL) {
      return false;
    }
    server->sent_chunk_keys = new_keys;
    server->sent_chunk_capacity = new_cap;
  }

  server->sent_chunk_keys[server->sent_chunk_count++] = key;
  return true;
}

static void sent_chunk_remove_at(ServerCore *server, size_t idx) {
  if (idx >= server->sent_chunk_count) {
    return;
  }

  server->sent_chunk_keys[idx] = server->sent_chunk_keys[server->sent_chunk_count - 1];
  server->sent_chunk_count--;
}

static void sent_chunk_clear(ServerCore *server) { server->sent_chunk_count = 0; }

static void server_queue_outgoing(ServerCore *server, const uint8_t *data, size_t size,
                                  uint8_t channel, NetReliability reliability) {
  ServerOutgoingNode *node;

  if (server == NULL || data == NULL || size == 0) {
    return;
  }

  node = (ServerOutgoingNode *)calloc(1, sizeof(ServerOutgoingNode));
  if (node == NULL) {
    return;
  }

  node->message.data = (uint8_t *)malloc(size);
  if (node->message.data == NULL) {
    free(node);
    return;
  }

  memcpy(node->message.data, data, size);
  node->message.size = size;
  node->message.channel = channel;
  node->message.reliability = reliability;
  node->message.client_id = 0;

  pthread_mutex_lock(&server->outgoing_mutex);
  if (server->outgoing_tail == NULL) {
    server->outgoing_head = node;
    server->outgoing_tail = node;
  } else {
    server->outgoing_tail->next = node;
    server->outgoing_tail = node;
  }
  pthread_mutex_unlock(&server->outgoing_mutex);

  if (server->net != NULL) {
    Net_SendPacket(server->net, data, size, channel, reliability, 0);
  }
}

bool ServerCore_PollOutgoing(ServerCore *server, ServerOutgoingMessage *out_message) {
  ServerOutgoingNode *node;

  if (server == NULL || out_message == NULL) {
    return false;
  }

  memset(out_message, 0, sizeof(*out_message));

  pthread_mutex_lock(&server->outgoing_mutex);
  node = server->outgoing_head;
  if (node != NULL) {
    server->outgoing_head = node->next;
    if (server->outgoing_head == NULL) {
      server->outgoing_tail = NULL;
    }
  }
  pthread_mutex_unlock(&server->outgoing_mutex);

  if (node == NULL) {
    return false;
  }

  *out_message = node->message;
  free(node);
  return true;
}

void ServerCore_FreeOutgoing(ServerOutgoingMessage *message) {
  if (message == NULL) {
    return;
  }
  free(message->data);
  memset(message, 0, sizeof(*message));
}

static void server_send_welcome(ServerCore *server) {
  NetWelcome welcome = {
      .seed = server->config.seed,
      .render_distance = (int32_t)server->config.render_distance,
      .tick_rate = server->config.tick_rate,
  };
  uint8_t buffer[64];
  size_t size = 0;

  if (!Protocol_EncodeWelcome(server->next_sequence++, server->tick_counter, &welcome, buffer,
                              sizeof(buffer), &size)) {
    return;
  }

  server_queue_outgoing(server, buffer, size, 0, NET_RELIABLE);
}

static void server_send_disconnect(ServerCore *server, const char *reason) {
  NetDisconnect disconnect_msg;
  uint8_t buffer[256];
  size_t size = 0;

  memset(&disconnect_msg, 0, sizeof(disconnect_msg));
  if (reason != NULL) {
    strncpy(disconnect_msg.reason, reason, RVNET_MAX_DISCONNECT_REASON - 1);
  } else {
    strncpy(disconnect_msg.reason, "Disconnected", RVNET_MAX_DISCONNECT_REASON - 1);
  }

  if (!Protocol_EncodeDisconnect(server->next_sequence++, server->tick_counter, &disconnect_msg,
                                 buffer, sizeof(buffer), &size)) {
    return;
  }

  server_queue_outgoing(server, buffer, size, 0, NET_RELIABLE);
}

static void server_send_player_state(ServerCore *server) {
  AuthoritativePlayerState state = {
      .tick_id = server->tick_counter,
      .input_tick_id = server->last_applied_input_tick,
      .position_x = server->player.position.x,
      .position_y = server->player.position.y,
      .position_z = server->player.position.z,
      .velocity_x = server->player.velocity.x,
      .velocity_y = server->player.velocity.y,
      .velocity_z = server->player.velocity.z,
      .yaw = server->player.yaw,
      .pitch = server->player.pitch,
      .on_ground = server->player.on_ground ? 1u : 0u,
      .world_time = server->world.world_time,
  };
  uint8_t buffer[128];
  size_t size = 0;

  if (!Protocol_EncodePlayerState(server->next_sequence++, server->tick_counter, &state, buffer,
                                  sizeof(buffer), &size)) {
    return;
  }

  server_queue_outgoing(server, buffer, size, 1, NET_RELIABLE);
}

static void server_send_chunk_unload(ServerCore *server, int cx, int cz) {
  NetChunkUnload unload = {.cx = cx, .cz = cz};
  uint8_t buffer[64];
  size_t size = 0;

  if (!Protocol_EncodeChunkUnload(server->next_sequence++, server->tick_counter, &unload, buffer,
                                  sizeof(buffer), &size)) {
    return;
  }

  server_queue_outgoing(server, buffer, size, 0, NET_RELIABLE);
}

static void server_send_block_delta(ServerCore *server, int x, int y, int z) {
  NetBlockDelta delta = {
      .x = x,
      .y = y,
      .z = z,
      .block_id = World_GetBlock(&server->world, x, y, z),
      .skylight = (uint8_t)World_GetSkyLight(&server->world, x, y, z),
  };
  uint8_t buffer[64];
  size_t size = 0;

  if (!Protocol_EncodeBlockDelta(server->next_sequence++, server->tick_counter, &delta, buffer,
                                 sizeof(buffer), &size)) {
    return;
  }

  server_queue_outgoing(server, buffer, size, 0, NET_RELIABLE);
}

static void server_send_chunk_data(ServerCore *server, const Chunk *chunk) {
  NetChunkData data;
  size_t payload_size = Protocol_HeaderSize() + 8 + WORLD_CHUNK_VOLUME + WORLD_CHUNK_LIGHT_BYTES +
                        (WORLD_CHUNK_SIZE_X * WORLD_CHUNK_SIZE_Z);
  uint8_t *buffer;
  size_t size = 0;

  if (chunk == NULL) {
    return;
  }

  data.cx = chunk->cx;
  data.cz = chunk->cz;
  data.blocks = chunk->blocks;
  data.skylight = chunk->skylight;
  data.heightmap = chunk->heightmap;

  buffer = (uint8_t *)malloc(payload_size);
  if (buffer == NULL) {
    return;
  }

  if (!Protocol_EncodeChunkData(server->next_sequence++, server->tick_counter, &data, buffer,
                                payload_size, &size)) {
    free(buffer);
    return;
  }

  server_queue_outgoing(server, buffer, size, 0, NET_RELIABLE);
  free(buffer);
}

static void server_apply_interactions(ServerCore *server, const GameplayInputCmd *input) {
  Vector3 eye;
  Vector3 dir;
  VoxelRaycastHit hit;

  if (server == NULL || input == NULL) {
    return;
  }

  eye = Player_GetEyePosition(&server->player);
  dir = Player_GetLookDirection(&server->player);

  if ((input->buttons & GAMEPLAY_INPUT_LEFT_CLICK) != 0) {
    if (Raycast_VoxelForPlacement(&server->world, eye, dir, PLAYER_REACH_DISTANCE, &hit) &&
        hit.hit) {
      if (server->gameplay_mode == GAMEPLAY_MODE_CREATIVE) {
        if (hit.block_id != BLOCK_BEDROCK &&
            World_SetBlock(&server->world, hit.block_x, hit.block_y, hit.block_z, BLOCK_AIR)) {
          server_send_block_delta(server, hit.block_x, hit.block_y, hit.block_z);
          server_reset_break_state(server);
        }
      } else {
        int durability = Block_GetDurability(hit.block_id);
        if (hit.block_id == BLOCK_BEDROCK || durability <= 0) {
          server_reset_break_state(server);
          return;
        }

        if (!server->break_state_active || server->break_block_x != hit.block_x ||
            server->break_block_y != hit.block_y || server->break_block_z != hit.block_z) {
          server->break_state_active = true;
          server->break_block_x = hit.block_x;
          server->break_block_y = hit.block_y;
          server->break_block_z = hit.block_z;
          server->break_damage = 0.0f;
          server->break_last_recover_tick = 0u;
        }

        server->break_damage += SERVER_BREAK_DAMAGE_PER_HIT;
        server->break_last_tick = server->tick_counter;
        if (server->break_damage >= (float)durability &&
            World_SetBlock(&server->world, hit.block_x, hit.block_y, hit.block_z, BLOCK_AIR)) {
          server_send_block_delta(server, hit.block_x, hit.block_y, hit.block_z);
          server_reset_break_state(server);
        }
      }
    }
  }

  if ((input->buttons & GAMEPLAY_INPUT_RIGHT_CLICK) != 0) {
    int px, py, pz;
    BoundingBox block_box;
    BoundingBox player_box;

    if (!(Raycast_VoxelForPlacement(&server->world, eye, dir, PLAYER_REACH_DISTANCE, &hit) &&
          hit.hit)) {
      return;
    }

    px = hit.block_x;
    py = hit.block_y;
    pz = hit.block_z;

    if (!Block_IsReplaceable(hit.block_id)) {
      px += hit.normal_x;
      py += hit.normal_y;
      pz += hit.normal_z;
    }

    if (py < 0 || py >= WORLD_MAX_HEIGHT ||
        !Block_IsReplaceable(World_GetBlock(&server->world, px, py, pz))) {
      return;
    }

    block_box = (BoundingBox){
        .min = {(float)px, (float)py, (float)pz},
        .max = {(float)px + 1.0f, (float)py + 1.0f, (float)pz + 1.0f},
    };
    player_box = Player_GetBoundsAt(&server->player, server->player.position);
    if (!CheckCollisionBoxes(player_box, block_box) &&
        World_SetBlock(&server->world, px, py, pz, server->player.selected_block)) {
      server_send_block_delta(server, px, py, pz);
    }
  }
}

static bool server_is_finite_move(const NetPlayerMove *move) {
  if (move == NULL) {
    return false;
  }

  return isfinite(move->position_x) && isfinite(move->position_y) && isfinite(move->position_z) &&
         isfinite(move->velocity_x) && isfinite(move->velocity_y) && isfinite(move->velocity_z) &&
         isfinite(move->yaw) && isfinite(move->pitch);
}

static bool server_apply_client_move(ServerCore *server, const NetPlayerMove *move) {
  Vector3 target_position;
  Vector3 target_velocity;
  Vector3 delta;
  float horizontal_delta;
  float vertical_delta;
  float max_horizontal;
  float max_vertical;
  uint32_t tick_gap = 1u;
  BoundingBox target_bounds;

  if (server == NULL || move == NULL || !server_is_finite_move(move)) {
    return false;
  }

  if (server->last_applied_input_tick != 0u) {
    if (move->tick_id <= server->last_applied_input_tick) {
      return true;
    }
    tick_gap = move->tick_id - server->last_applied_input_tick;
  }
  if (tick_gap > SERVER_MOVE_MAX_TICK_GAP) {
    tick_gap = SERVER_MOVE_MAX_TICK_GAP;
  }

  target_position = (Vector3){move->position_x, move->position_y, move->position_z};
  target_velocity = (Vector3){move->velocity_x, move->velocity_y, move->velocity_z};

  if (target_position.y < -64.0f || target_position.y > (float)WORLD_MAX_HEIGHT + 64.0f) {
    return false;
  }

  delta = Vector3Subtract(target_position, server->player.position);
  horizontal_delta = sqrtf(delta.x * delta.x + delta.z * delta.z);
  vertical_delta = fabsf(delta.y);
  max_horizontal = SERVER_MOVE_MAX_HORIZ_PER_TICK * (float)tick_gap;
  max_vertical = SERVER_MOVE_MAX_VERT_PER_TICK * (float)tick_gap;

  if (horizontal_delta > max_horizontal || vertical_delta > max_vertical) {
    return false;
  }

  target_bounds = Player_GetBoundsAt(&server->player, target_position);
  if (World_CheckAABB(&server->world, target_bounds)) {
    return false;
  }

  server->player.previous_position = server->player.position;
  server->player.previous_yaw = server->player.yaw;
  server->player.previous_pitch = server->player.pitch;
  server->player.position = target_position;
  server->player.velocity = target_velocity;
  server->player.yaw = move->yaw;
  server->player.pitch = move->pitch;
  server->player.on_ground = move->on_ground != 0u;
  server->last_applied_input_tick = move->tick_id;
  return true;
}

static void server_stream_visible_chunks(ServerCore *server) {
  int player_cx, player_cz;
  int rd;

  if (server == NULL || !server->client_ready) {
    return;
  }

  player_cx = floor_div16((int)floorf(server->player.position.x));
  player_cz = floor_div16((int)floorf(server->player.position.z));
  rd = (int)server->client_render_distance;
  if (rd < 2) {
    rd = 2;
  }

  for (int dz = -rd; dz <= rd; dz++) {
    for (int dx = -rd; dx <= rd; dx++) {
      int cx = player_cx + dx;
      int cz = player_cz + dz;
      int64_t key = make_chunk_key(cx, cz);
      const Chunk *chunk = World_GetChunkConst(&server->world, cx, cz);
      if (chunk != NULL && !sent_chunk_contains(server, key)) {
        server_send_chunk_data(server, chunk);
        sent_chunk_add(server, key);
      }
    }
  }

  size_t i = 0;
  while (i < server->sent_chunk_count) {
    int64_t key = server->sent_chunk_keys[i];
    int cx = (int)(key >> 32);
    int cz = (int)(int32_t)(key & 0xFFFFFFFFu);
    int dx = abs(cx - player_cx);
    int dz = abs(cz - player_cz);
    bool inside = (dx <= rd && dz <= rd);
    const Chunk *chunk = World_GetChunkConst(&server->world, cx, cz);

    if (!inside || chunk == NULL) {
      server_send_chunk_unload(server, cx, cz);
      sent_chunk_remove_at(server, i);
      continue;
    }
    i++;
  }
}

static void server_consume_latest_input(ServerCore *server, GameplayInputCmd *out_input) {
  bool accepted_pending = false;

  pthread_mutex_lock(&server->input_mutex);
  if (server->pending_input_available) {
    /*
     * Ignore stale pending inputs. Reliable transport keeps ordering, but this
     * guard protects local queue bursts and future transport changes.
     */
    if (server->pending_input.tick_id >= server->current_input.tick_id) {
      server->current_input = server->pending_input;
      accepted_pending = true;
    }
    server->pending_input_available = false;
  }

  *out_input = server->current_input;

  /*
   * Consume one-shot input fields so they are not reapplied on later ticks
   * when no fresh input packet arrives.
   *
   * Held movement buttons remain in current_input and continue to apply until
   * an updated command is received (matching latest-known-input semantics).
   */
  server->current_input.look_delta_x = 0.0f;
  server->current_input.look_delta_y = 0.0f;
  server->current_input.buttons &=
      (uint8_t)~(GAMEPLAY_INPUT_LEFT_CLICK | GAMEPLAY_INPUT_RIGHT_CLICK);

  if (!accepted_pending) {
    out_input->look_delta_x = 0.0f;
    out_input->look_delta_y = 0.0f;
    out_input->buttons &= (uint8_t)~(GAMEPLAY_INPUT_LEFT_CLICK | GAMEPLAY_INPUT_RIGHT_CLICK);
  }

  pthread_mutex_unlock(&server->input_mutex);
}

static bool server_consume_latest_move(ServerCore *server, NetPlayerMove *out_move) {
  bool accepted_pending = false;

  if (server == NULL || out_move == NULL) {
    return false;
  }

  pthread_mutex_lock(&server->input_mutex);
  if (server->pending_move_available) {
    if (server->pending_move.tick_id > server->current_move.tick_id) {
      server->current_move = server->pending_move;
      accepted_pending = true;
    }
    server->pending_move_available = false;
  }
  *out_move = server->current_move;
  pthread_mutex_unlock(&server->input_mutex);
  return accepted_pending;
}

void ServerCore_SubmitInput(ServerCore *server, int client_id, const GameplayInputCmd *input) {
  (void)client_id;
  if (server == NULL || input == NULL) {
    return;
  }

  pthread_mutex_lock(&server->input_mutex);
  if (!server->pending_input_available || input->tick_id >= server->pending_input.tick_id) {
    server->pending_input = *input;
    server->pending_input_available = true;
  }
  pthread_mutex_unlock(&server->input_mutex);
}

static void server_submit_move(ServerCore *server, const NetPlayerMove *move) {
  if (server == NULL || move == NULL) {
    return;
  }

  pthread_mutex_lock(&server->input_mutex);
  if (!server->pending_move_available || move->tick_id >= server->pending_move.tick_id) {
    server->pending_move = *move;
    server->pending_move_available = true;
  }
  pthread_mutex_unlock(&server->input_mutex);
}

static void server_process_incoming(ServerCore *server) {
  NetEvent event;

  if (server == NULL || server->net == NULL) {
    return;
  }

  Net_Update(server->net, 0);

  while (Net_PollEvent(server->net, &event)) {
    if (event.type == NET_EVENT_CONNECTED) {
      server->has_client = true;
    } else if (event.type == NET_EVENT_DISCONNECTED) {
      server->has_client = false;
      server->client_ready = false;
      sent_chunk_clear(server);
      memset(&server->current_input, 0, sizeof(server->current_input));
      server->pending_input_available = false;
      memset(&server->current_move, 0, sizeof(server->current_move));
      server->pending_move_available = false;
      server->last_applied_input_tick = 0;
      server->gameplay_mode = GAMEPLAY_MODE_CREATIVE;
      server->fly_enabled = false;
      server->player.gameplay_mode = server->gameplay_mode;
      server->player.fly_enabled = server->fly_enabled;
      server_reset_break_state(server);
    } else if (event.type == NET_EVENT_MESSAGE) {
      if (event.message_type == NET_MSG_C2S_HELLO) {
        server->has_client = true;
        server->client_ready = true;
        server->client_render_distance = event.payload.hello.requested_render_distance;
        if (server->client_render_distance == 0 ||
            server->client_render_distance > (uint32_t)server->config.render_distance) {
          server->client_render_distance = (uint32_t)server->config.render_distance;
        }
        sent_chunk_clear(server);
        memset(&server->current_input, 0, sizeof(server->current_input));
        server->pending_input_available = false;
        server->last_applied_input_tick = 0;
        memset(&server->current_move, 0, sizeof(server->current_move));
        server->pending_move_available = false;
        server->gameplay_mode = GAMEPLAY_MODE_CREATIVE;
        server->fly_enabled = false;
        server_reset_break_state(server);
        server_send_welcome(server);
        server_send_player_state(server);
      } else if (event.message_type == NET_MSG_C2S_INPUT_CMD && server->client_ready) {
        ServerCore_SubmitInput(server, 0, &event.payload.input_cmd);
      } else if (event.message_type == NET_MSG_C2S_PLAYER_MOVE && server->client_ready) {
        server_submit_move(server, &event.payload.player_move);
      }
    }
  }
}

void ServerCore_Tick(ServerCore *server) {
  GameplayInputCmd input = {0};
  NetPlayerMove move = {0};
  bool has_new_move = false;
  bool should_send_player_state = false;

  if (server == NULL || !ServerCore_IsRunning(server)) {
    return;
  }

  server_process_incoming(server);
  if (!server->has_client || !server->client_ready) {
    return;
  }

  server_consume_latest_input(server, &input);
  if (input.gameplay_mode == (uint8_t)GAMEPLAY_MODE_SURVIVAL) {
    server->gameplay_mode = GAMEPLAY_MODE_SURVIVAL;
  } else {
    server->gameplay_mode = GAMEPLAY_MODE_CREATIVE;
  }
  server->fly_enabled =
      (server->gameplay_mode == GAMEPLAY_MODE_CREATIVE) && (input.fly_enabled != 0u);
  server->player.gameplay_mode = server->gameplay_mode;
  server->player.fly_enabled = server->fly_enabled;

  has_new_move = server_consume_latest_move(server, &move);
  if (has_new_move && !server_apply_client_move(server, &move)) {
    should_send_player_state = true;
  }

  server_decay_break_state(server);
  server->player.selected_block = input.selected_block;
  server_apply_interactions(server, &input);

  World_Update(&server->world, server->player.position, 1.0f / (float)server->config.tick_rate);
  server_stream_visible_chunks(server);
  if (should_send_player_state || ((server->tick_counter % 20u) == 0u)) {
    server_send_player_state(server);
  }

  server->tick_counter++;
}

static void *server_thread_main(void *arg) {
  ServerCore *server = (ServerCore *)arg;
  double dt = 1.0 / (double)server->config.tick_rate;
  double next_tick = now_seconds();

  while (ServerCore_IsRunning(server)) {
    double now = now_seconds();
    if (now >= next_tick) {
      ServerCore_Tick(server);
      next_tick += dt;
      if (now - next_tick > 0.5) {
        next_tick = now + dt;
      }
    } else {
      sleep_millis(1);
    }
  }

  return NULL;
}

bool ServerCore_StartThread(ServerCore *server) {
  if (server == NULL || server->thread_started) {
    return false;
  }

  atomic_store(&server->running, true);
  if (pthread_create(&server->thread, NULL, server_thread_main, server) != 0) {
    return false;
  }
  server->thread_started = true;
  return true;
}

void ServerCore_Stop(ServerCore *server) {
  if (server == NULL) {
    return;
  }

  atomic_store(&server->running, false);
  if (server->thread_started) {
    pthread_join(server->thread, NULL);
    server->thread_started = false;
  }
}

bool ServerCore_IsRunning(ServerCore *server) {
  if (server == NULL) {
    return false;
  }
  return atomic_load(&server->running);
}

static void server_outgoing_clear(ServerCore *server) {
  ServerOutgoingMessage msg;
  while (ServerCore_PollOutgoing(server, &msg)) {
    ServerCore_FreeOutgoing(&msg);
  }
}

bool ServerCore_Init(ServerCore *server, const ServerConfig *config, NetEndpoint *net,
                     bool take_net_ownership) {
  int spawn_y;
  Vector3 spawn;

  if (server == NULL || config == NULL) {
    return false;
  }

  memset(server, 0, sizeof(*server));
  server->config = *config;
  if (server->config.tick_rate <= 0) {
    server->config.tick_rate = (int)GAME_TICK_RATE;
  }
  if (server->config.max_clients <= 0) {
    server->config.max_clients = 1;
  }
  if (server->config.max_clients > 1) {
    server->config.max_clients = 1;
  }
  if (server->config.render_distance < 2) {
    server->config.render_distance = 2;
  }
  if (server->config.render_distance > 12) {
    server->config.render_distance = 12;
  }

  if (pthread_mutex_init(&server->input_mutex, NULL) != 0) {
    return false;
  }
  if (pthread_mutex_init(&server->outgoing_mutex, NULL) != 0) {
    pthread_mutex_destroy(&server->input_mutex);
    return false;
  }

  server->net = net;
  server->owns_net = take_net_ownership;
  server->client_render_distance = (uint32_t)server->config.render_distance;
  server->next_sequence = 1;
  atomic_store(&server->running, true);

  World_InitServer(&server->world, server->config.seed, server->config.render_distance);
  server->world_initialized = true;

  spawn_y = World_GetTopY(&server->world, 0, 0);
  spawn = (Vector3){0.5f, (float)spawn_y + 2.0f, 0.5f};
  Player_Init(&server->player, spawn);
  server->gameplay_mode = GAMEPLAY_MODE_CREATIVE;
  server->fly_enabled = false;
  server->player.gameplay_mode = server->gameplay_mode;
  server->player.fly_enabled = server->fly_enabled;
  server_reset_break_state(server);

  return true;
}

void ServerCore_Shutdown(ServerCore *server) {
  if (server == NULL) {
    return;
  }

  if (ServerCore_IsRunning(server) || server->thread_started) {
    ServerCore_Stop(server);
  }

  server_send_disconnect(server, "Server shutting down");
  server_outgoing_clear(server);

  if (server->net != NULL && server->owns_net) {
    Net_Close(server->net);
    Net_Destroy(server->net);
  }
  server->net = NULL;

  if (server->world_initialized) {
    World_Shutdown(&server->world);
    server->world_initialized = false;
  }

  free(server->sent_chunk_keys);
  server->sent_chunk_keys = NULL;
  server->sent_chunk_count = 0;
  server->sent_chunk_capacity = 0;

  pthread_mutex_destroy(&server->input_mutex);
  pthread_mutex_destroy(&server->outgoing_mutex);
}

AuthoritativePlayerState ServerCore_GetAuthoritativePlayerState(ServerCore *server) {
  AuthoritativePlayerState state = {0};
  if (server == NULL) {
    return state;
  }

  state.tick_id = server->tick_counter;
  state.input_tick_id = server->last_applied_input_tick;
  state.position_x = server->player.position.x;
  state.position_y = server->player.position.y;
  state.position_z = server->player.position.z;
  state.velocity_x = server->player.velocity.x;
  state.velocity_y = server->player.velocity.y;
  state.velocity_z = server->player.velocity.z;
  state.yaw = server->player.yaw;
  state.pitch = server->player.pitch;
  state.on_ground = server->player.on_ground ? 1u : 0u;
  state.world_time = server->world.world_time;
  return state;
}
