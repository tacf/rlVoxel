#ifndef RLVOXEL_GAME_H
#define RLVOXEL_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include <raylib.h>

#include "gfx/clouds.h"
#include "gfx/renderer.h"
#include "game/game_input.h"
#include "net/net.h"
#include "game/player.h"
#include "net/protocol.h"
#include "server/server_core.h"
#include "ui/ui.h"
#include "world/world.h"

#define GAME_PREDICTION_HISTORY_SIZE 256u
#define GAME_INPUT_HISTORY_SIZE 256u
#define GAME_PENDING_BREAK_OPS_MAX 64u

typedef struct PredictedPlayerSample {
  uint32_t tick_id;
  Vector3 position;
  Vector3 velocity;
  bool on_ground;
  bool valid;
} PredictedPlayerSample;

typedef struct GameplayInputHistoryEntry {
  uint32_t tick_id;
  GameplayInputCmd cmd;
  bool valid;
} GameplayInputHistoryEntry;

typedef struct PendingBreakOp {
  int x;
  int y;
  int z;
  uint8_t previous_block_id;
  uint8_t previous_skylight;
  uint32_t sent_tick;
  bool active;
} PendingBreakOp;

typedef struct Game {
  World world;
  Player player;
  Camera3D camera;
  Renderer renderer;
  Clouds clouds;

  Texture2D terrain_texture;
  Font font;
  UiContext ui;

  int64_t seed;
  int network_tick_rate;
  uint32_t network_tick_counter;
  uint32_t network_sequence;
  bool remote_mode;
  bool connected;
  bool welcome_received;
  uint32_t last_authoritative_tick;
  bool has_authoritative_state;
  NetPlayerMove last_sent_move;
  uint32_t last_sent_move_tick_id;
  bool has_last_sent_move;
  GameplayInputCmd last_sent_input_cmd;
  uint32_t last_sent_input_tick_id;
  bool has_last_sent_input_cmd;
  uint32_t last_survival_break_send_tick;
  GameplayMode gameplay_mode;
  bool fly_enabled;
  PredictedPlayerSample prediction_history[GAME_PREDICTION_HISTORY_SIZE];
  GameplayInputHistoryEntry input_history[GAME_INPUT_HISTORY_SIZE];
  PendingBreakOp pending_break_ops[GAME_PENDING_BREAK_OPS_MAX];
  float view_yaw;
  float view_pitch;
  bool view_initialized;
  bool suppress_next_frame_look;

  bool break_visual_active;
  int break_visual_x;
  int break_visual_y;
  int break_visual_z;
  float break_visual_damage;
  uint32_t break_visual_last_tick;
  uint32_t break_visual_last_recover_tick;

  NetEndpoint *net;
  bool net_initialized;
  bool owns_net;

  ServerCore internal_server;
  bool internal_server_initialized;

  bool cursor_locked;
  bool pending_lock_request;
  bool lock_before_escape_menu;
  bool quit_requested;

  // Escape menu state
  bool show_escape_menu;
  bool show_options;

  // Options
  bool clouds_enabled;
  bool pixel_look_enabled;

  // Debug menu state
  bool show_debug_menu;
  bool show_profiler_stats;
  bool show_frame_graph;
  bool show_telemetry;
  bool show_net_profiler;

  /* Internal subsystem/resource init flags for safe partial unwind. */
  bool profiler_initialized;
  bool profiler_renderer_initialized;
  bool telemetry_initialized;
  bool imgui_initialized;
  bool ui_initialized;
  bool world_initialized;
  bool renderer_initialized;
} Game;

bool Game_Init(Game *game, int64_t seed, int render_distance, const char *connect_host,
               uint16_t connect_port);
void Game_Shutdown(Game *game);

void Game_Tick(Game *game, const GameInputSnapshot *input, float tick_dt);
void Game_ApplyFrameLook(Game *game, GameInputSnapshot *frame_input);
void Game_Draw(Game *game, float alpha);
void Game_DrawHUD(Game *game);

#endif
