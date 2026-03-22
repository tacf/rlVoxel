#include "game/game.h"

#include <math.h>
#include <stdint.h>

#include <raymath.h>
#include <stdlib.h>

#include "constants.h"
#include "gfx/clouds.h"
#include "gfx/renderer.h"
#include "gfx/selection_highlight.h"
#include "game/game_input.h"
#include "game/player.h"
#include "game/raycast.h"
#include "raylib.h"
#include "rlcimgui.h"
#include "ui/hud.h"
#include "ui/ui.h"
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "world/blocks.h"
#include "world/world.h"
#include "profiling/profiler.h"
#include "profiling/profiler_renderer.h"
#include "diagnostics/telemetry.h"

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
  set_camera_from_pose(game, game->player.position, game->player.yaw, game->player.pitch);
}

/*
 * Interpolate camera pose between previous and current player states.
 * This smooths rendering when simulation runs at a fixed tick rate.
 */
static void sync_camera_interpolated(Game *game, float alpha) {
  alpha = Clamp(alpha, 0.0f, 1.0f);

  Vector3 position = Vector3Lerp(game->player.previous_position, game->player.position, alpha);

  float yaw = game->player.previous_yaw + (game->player.yaw - game->player.previous_yaw) * alpha;
  float pitch =
      game->player.previous_pitch + (game->player.pitch - game->player.previous_pitch) * alpha;

  set_camera_from_pose(game, position, yaw, pitch);
}

static void set_cursor_locked(Game *game, bool locked) {
  game->cursor_locked = locked;

  if (locked) {
    DisableCursor();
  } else {
    EnableCursor();
  }
}

static void open_debug_menu(Game *game) {
  if (game->show_debug_menu) {
    return;
  }

  game->show_debug_menu = true;
  game->lock_before_debug_menu = game->cursor_locked || game->pending_lock_request;
  game->pending_lock_request = false;
  set_cursor_locked(game, false);
}

static void close_debug_menu(Game *game) {
  if (!game->show_debug_menu) {
    return;
  }

  game->show_debug_menu = false;
  if (game->lock_before_debug_menu) {
    game->pending_lock_request = true;
    set_cursor_locked(game, true);
  } else {
    game->pending_lock_request = false;
    set_cursor_locked(game, false);
  }
}

static void game_draw_target_block_highlight(Game *game, const Camera3D *camera) {
  if (game == NULL || camera == NULL || !game->cursor_locked || game->show_debug_menu) {
    return;
  }

  Vector3 ray_direction = Vector3Subtract(camera->target, camera->position);
  VoxelRaycastHit hit;
  if (Raycast_VoxelForPlacement(&game->world, camera->position, ray_direction,
                                PLAYER_REACH_DISTANCE, &hit) &&
      hit.hit) {
    SelectionHighlight_Draw(&hit);
  }
}

static void handle_block_interaction(Game *game, bool left_click_pressed,
                                     bool right_click_pressed) {
  Vector3 eye = Player_GetEyePosition(&game->player);
  Vector3 dir = Player_GetLookDirection(&game->player);

  VoxelRaycastHit hit;

  if (left_click_pressed) {
    if (Raycast_VoxelForPlacement(&game->world, eye, dir, PLAYER_REACH_DISTANCE, &hit) && hit.hit) {
      if (hit.block_id != BLOCK_BEDROCK) {
        World_SetBlock(&game->world, hit.block_x, hit.block_y, hit.block_z, BLOCK_AIR);
      }
    }
  }

  if (right_click_pressed) {
    if (Raycast_VoxelForPlacement(&game->world, eye, dir, PLAYER_REACH_DISTANCE, &hit) && hit.hit) {
      int px = hit.block_x;
      int py = hit.block_y;
      int pz = hit.block_z;

      if (!Block_IsReplaceable(hit.block_id)) {
        px += hit.normal_x;
        py += hit.normal_y;
        pz += hit.normal_z;
      }

      if (py >= 0 && py < WORLD_MAX_HEIGHT &&
          Block_IsReplaceable(World_GetBlock(&game->world, px, py, pz))) {
        BoundingBox block_box = {
            .min = {(float)px, (float)py, (float)pz},
            .max = {(float)px + 1.0f, (float)py + 1.0f, (float)pz + 1.0f},
        };

        BoundingBox player_box = Player_GetBoundsAt(&game->player, game->player.position);
        if (!CheckCollisionBoxes(player_box, block_box)) {
          World_SetBlock(&game->world, px, py, pz, game->player.selected_block);
        }
      }
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

  Clouds_Draw(&pass->game->clouds, camera, pass->ambient);
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

  rligEnd();
}

static void game_draw_debug_ui_pass(void *ctx) {
  Game *game = (Game *)ctx;
  if (game == NULL) {
    return;
  }

  game_draw_debug_ui(game);
}

bool Game_Init(Game *game, int64_t seed, int render_distance) {
  if (!game) {
    return false;
  }

  *game = (Game){0};
  game->seed = seed;
  game->cursor_locked = true;
  game->pending_lock_request = true;
  game->lock_before_debug_menu = true;

  game->show_debug_menu = false;
  game->show_profiler_stats = false;
  game->show_frame_graph = false;
  game->show_telemetry = false;

  Profiler_Init();
  game->profiler_initialized = true;
  Profiler_SetEnabled(true);

  ProfilerRenderer_Init();
  game->profiler_renderer_initialized = true;
  Telemetry_Init();
  game->telemetry_initialized = true;

  rligSetup(true);
  game->imgui_initialized = true;

  ImGuiIO *io = igGetIO();
  io->ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

  if (!Renderer_Init(&game->renderer)) {
    Game_Shutdown(game);
  }
  game->renderer_initialized = true;

  if (!UI_Init(&game->ui, UI_DEFAULT_MAX_NODES, UI_DEFAULT_TEXT_CAPACITY, UI_DEFAULT_MAX_STATES)) {
    Game_Shutdown(game);
  }
  UI_SetReferenceResolution(&game->ui, 1280.0f, 720.0f);
  game->ui_initialized = true;

  game->terrain_texture = LoadTexture("assets/atlas.png");
  if (game->terrain_texture.id == 0) {
    Game_Shutdown(game);
  }

  game->font = LoadFontEx("assets/fonts/default.ttf", 20, NULL, 0);
  if (game->font.texture.id == 0) {
    Game_Shutdown(game);
  }
  SetTextureFilter(game->font.texture, TEXTURE_FILTER_POINT);

  SetTextureFilter(game->terrain_texture, TEXTURE_FILTER_POINT);

  World_Init(&game->world, seed, render_distance, game->terrain_texture);
  game->world_initialized = true;

  int spawn_y = World_GetTopY(&game->world, 0, 0);
  Vector3 spawn = {0.5f, (float)spawn_y + 2.0f, 0.5f};
  Player_Init(&game->player, spawn);

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

  Game_Shutdown(game);
}

void Game_Shutdown(Game *game) {
  if (!game) {
    exit(1);
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

  if (game->imgui_initialized) {
    rligShutdown();
    game->imgui_initialized = false;
  }
  exit(0);
}

void Game_Tick(Game *game, const GameInputSnapshot *input, float tick_dt) {
  if (!input) {
    return;
  }

  Profiler_BeginSection("Update");

  bool left_click_pressed = input->left_click_pressed;
  bool right_click_pressed = input->right_click_pressed;
  bool consumed_relock_click = false;

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
    } else if (game->cursor_locked || game->pending_lock_request) {
      game->pending_lock_request = false;
      set_cursor_locked(game, false);
    } else {
      game->pending_lock_request = true;
      set_cursor_locked(game, true);
    }
  }

  if (!game->show_debug_menu && !game->cursor_locked && left_click_pressed) {
    game->pending_lock_request = true;
    set_cursor_locked(game, true);
    consumed_relock_click = true;
  }

  if (!game->show_debug_menu && game->cursor_locked && !IsWindowFocused()) {
    game->pending_lock_request = true;
  }

  bool click_retrying_lock = !game->show_debug_menu && game->pending_lock_request &&
                             (left_click_pressed || right_click_pressed);
  if (click_retrying_lock) {
    consumed_relock_click = true;
  }

  if (game->pending_lock_request && !game->show_debug_menu) {
    if (IsWindowFocused() || click_retrying_lock) {
      set_cursor_locked(game, true);
    }

    if (IsWindowFocused()) {
      game->pending_lock_request = false;
    }
  }

  float hotbar_scroll =
      (!game->show_debug_menu && game->cursor_locked) ? input->mouse_wheel_delta : 0.0f;
  Player_ApplyHotbarScroll(&game->player, hotbar_scroll);

  Profiler_BeginSection("Player");
  Player_Update(&game->player, &game->world, input, tick_dt, game->cursor_locked);
  Profiler_EndSection();

  if (game->cursor_locked && !consumed_relock_click) {
    Profiler_BeginSection("BlockInteraction");
    handle_block_interaction(game, left_click_pressed, right_click_pressed);
    Profiler_EndSection();
  }

  Profiler_BeginSection("World");
  World_Update(&game->world, game->player.position, tick_dt);
  Profiler_EndSection();

  Clouds_Update(&game->clouds, tick_dt);

  Profiler_EndSection();
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

  if (game->cursor_locked) {
    int cx = GetScreenWidth() / 2;
    int cy = GetScreenHeight() / 2;

    DrawLine(cx - 8, cy, cx + 8, cy, (Color){0, 0, 0, 180});
    DrawLine(cx, cy - 8, cx, cy + 8, (Color){0, 0, 0, 180});
    DrawLine(cx - 7, cy, cx + 7, cy, RAYWHITE);
    DrawLine(cx, cy - 7, cx, cy + 7, RAYWHITE);
  }
}
