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
#include "server/server_core.h"
#include "ui/ui.h"
#include "world/world.h"

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

  NetEndpoint *net;
  bool net_initialized;
  bool owns_net;

  ServerCore internal_server;
  bool internal_server_initialized;

  bool cursor_locked;
  bool pending_lock_request;
  bool lock_before_debug_menu;
  bool lock_before_escape_menu;
  bool quit_requested;

  // Escape menu state
  bool show_escape_menu;
  bool show_options;

  // Options
  bool clouds_enabled;

  // Debug menu state
  bool show_debug_menu;
  bool show_profiler_stats;
  bool show_frame_graph;
  bool show_telemetry;

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
void Game_Draw(Game *game, float alpha);
void Game_DrawHUD(Game *game);

#endif
