#ifndef RLVOXEL_SERVER_CORE_H
#define RLVOXEL_SERVER_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>

#include "game/player.h"
#include "net/net.h"
#include "net/protocol.h"
#include "world/world.h"

/**
 * Server runtime mode.
 * - INTERNAL: in-process thread used by singleplayer.
 * - DEDICATED: headless server executable entrypoint.
 */
typedef enum ServerMode {
  SERVER_MODE_INTERNAL = 0,
  SERVER_MODE_DEDICATED = 1,
} ServerMode;

/** Immutable startup configuration for ServerCore_Init. */
typedef struct ServerConfig {
  int64_t seed;
  int render_distance;
  int tick_rate;
  int max_clients;
  char bind[64];
  uint16_t port;
  ServerMode mode;
  char save_directory[WORLD_SAVE_PATH_SIZE];
} ServerConfig;

/**
 * Authoritative server runtime state.
 * Owns world simulation, player simulation, and network-facing queues.
 */
typedef struct ServerCore {
  ServerConfig config;

  World world;
  Player player;
  bool world_initialized;
  WorldSaveInfo save_info;
  atomic_int save_status;

  NetEndpoint *net;
  bool owns_net;

  atomic_bool running;
  pthread_t thread;
  bool thread_started;

  pthread_mutex_t input_mutex;
  GameplayInputCmd pending_input;
  bool pending_input_available;
  GameplayInputCmd current_input;
  NetPlayerMove pending_move;
  bool pending_move_available;
  NetPlayerMove current_move;

  bool has_client;
  bool client_ready;
  uint32_t client_render_distance;
  uint32_t last_applied_input_tick;
  uint32_t tick_counter;
  uint32_t next_sequence;
  GameplayMode gameplay_mode;
  bool fly_enabled;

  bool break_state_active;
  int break_block_x;
  int break_block_y;
  int break_block_z;
  float break_damage;
  uint32_t break_last_tick;
  uint32_t break_last_recover_tick;

  int64_t *sent_chunk_keys;
  size_t sent_chunk_count;
  size_t sent_chunk_capacity;
} ServerCore;

/**
 * Initializes server runtime state and optionally binds a NetEndpoint.
 * take_net_ownership controls whether ServerCore_Shutdown destroys net.
 */
bool ServerCore_Init(ServerCore *server, const ServerConfig *config, NetEndpoint *net,
                     bool take_net_ownership);
/** Stops thread/net and releases all server-owned runtime resources. */
void ServerCore_Shutdown(ServerCore *server);

/** Starts fixed-tick server loop thread. */
bool ServerCore_StartThread(ServerCore *server);
/** Requests loop stop; thread is joined during shutdown/stop paths. */
void ServerCore_Stop(ServerCore *server);
/** True while server loop is running. */
bool ServerCore_IsRunning(ServerCore *server);

/** Runs one authoritative tick (used by thread loop and tests). */
void ServerCore_Tick(ServerCore *server);
/* Call from the server thread, or after ServerCore_Stop has joined it. */
bool ServerCore_Save(ServerCore *server);

#endif
