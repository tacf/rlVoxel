#ifndef RLVOXEL_GAME_H
#define RLVOXEL_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include <raylib.h>

#include "gfx/clouds.h"
#include "gfx/renderer.h"
#include "game/game_input.h"
#include "game/player.h"
#include "world/world.h"

typedef struct Game {
  World world;
  Player player;
  Camera3D camera;
  Renderer renderer;
  Clouds clouds;

  Texture2D terrain_texture;
  Font font;

  int64_t seed;
  bool cursor_locked;
  bool pending_lock_request;
  bool lock_before_debug_menu;

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
  bool world_initialized;
  bool renderer_initialized;
} Game;

bool Game_Init(Game *game, int64_t seed, int render_distance);
void Game_Shutdown(Game *game);

void Game_Tick(Game *game, const GameInputSnapshot *input, float tick_dt);
void Game_Draw(Game *game, float alpha);
void Game_DrawHUD(Game *game);

#endif
