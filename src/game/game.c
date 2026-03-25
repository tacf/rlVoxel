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

  if (game == NULL || game->net == NULL) {
    return false;
  }

  return Net_SendHello(game->net, game->network_sequence++, game->network_tick_counter, &hello);
}

static bool game_send_input(Game *game, const GameInputSnapshot *input, bool gameplay_enabled) {
  GameplayInputCmd cmd;

  if (game == NULL || game->net == NULL || !game->connected) {
    return false;
  }

  Game_BuildGameplayInputCmd(input, game->network_tick_counter, game->player.selected_block,
                             gameplay_enabled, &cmd);

  return Net_SendInputCmd(game->net, game->network_sequence++, game->network_tick_counter, &cmd);
}

static void game_apply_player_state(Game *game, const AuthoritativePlayerState *state) {
  if (game == NULL || state == NULL) {
    return;
  }

  game->player.previous_position = game->player.position;
  game->player.previous_yaw = game->player.yaw;
  game->player.previous_pitch = game->player.pitch;

  game->player.position = (Vector3){state->position_x, state->position_y, state->position_z};
  game->player.velocity = (Vector3){state->velocity_x, state->velocity_y, state->velocity_z};
  game->player.yaw = state->yaw;
  game->player.pitch = state->pitch;
  game->player.on_ground = state->on_ground != 0;

  World_SetTime(&game->world, state->world_time);
}

static void game_process_network(Game *game) {
  NetEvent event;

  if (game == NULL || game->net == NULL) {
    return;
  }

  Net_Update(game->net, 0);

  while (Net_PollEvent(game->net, &event)) {
    if (event.type == NET_EVENT_CONNECTED) {
      game->connected = true;
      game_send_hello(game, game->world.render_distance);
    } else if (event.type == NET_EVENT_DISCONNECTED) {
      game->connected = false;
      game->welcome_received = false;
    } else if (event.type == NET_EVENT_MESSAGE) {
      switch (event.message_type) {
      case NET_MSG_S2C_WELCOME:
        game->welcome_received = true;
        game->connected = true;
        game->seed = event.payload.welcome.seed;
        game->network_tick_rate = (event.payload.welcome.tick_rate > 0)
                                      ? event.payload.welcome.tick_rate
                                      : (int)GAME_TICK_RATE;
        if (event.payload.welcome.render_distance >= 2) {
          game->world.render_distance = event.payload.welcome.render_distance;
        }
        break;

      case NET_MSG_S2C_PLAYER_STATE:
        game_apply_player_state(game, &event.payload.player_state);
        break;

      case NET_MSG_S2C_CHUNK_DATA:
        World_ApplyChunkData(&game->world, event.payload.chunk_data.cx, event.payload.chunk_data.cz,
                             event.payload.chunk_data.blocks, sizeof(event.payload.chunk_data.blocks),
                             event.payload.chunk_data.skylight, sizeof(event.payload.chunk_data.skylight),
                             event.payload.chunk_data.heightmap,
                             sizeof(event.payload.chunk_data.heightmap));
        break;

      case NET_MSG_S2C_BLOCK_DELTA:
        World_ApplyBlockDelta(&game->world, event.payload.block_delta.x, event.payload.block_delta.y,
                              event.payload.block_delta.z, event.payload.block_delta.block_id,
                              event.payload.block_delta.skylight);
        break;

      case NET_MSG_S2C_CHUNK_UNLOAD:
        World_RemoveChunk(&game->world, event.payload.chunk_unload.cx, event.payload.chunk_unload.cz);
        break;

      case NET_MSG_S2C_DISCONNECT:
        game->quit_requested = true;
        break;

      default:
        break;
      }
    }
  }
}

static void game_draw_target_block_highlight(Game *game, const Camera3D *camera) {
  if (game == NULL || camera == NULL || !game->cursor_locked || game->show_debug_menu ||
      game_is_paused(game)) {
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
  game->remote_mode = (connect_host != NULL && connect_host[0] != '\0');
  game->cursor_locked = true;
  game->pending_lock_request = true;
  game->lock_before_debug_menu = true;

  game->show_escape_menu = false;
  game->show_options = false;
  game->clouds_enabled = true;
  game->quit_requested = false;
  game->lock_before_escape_menu = true;

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
    CloseWindow();
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
    } else if (game->show_options) {
      game->show_options = false;
      game->show_escape_menu = true;
    } else if (game->show_escape_menu) {
      close_escape_menu(game);
    } else {
      open_escape_menu(game);
    }
  }

  if (!game->show_debug_menu && !game_is_paused(game) && !game->cursor_locked &&
      left_click_pressed) {
    game->pending_lock_request = true;
    set_cursor_locked(game, true);
    consumed_relock_click = true;
  }

  if (!game->show_debug_menu && !game_is_paused(game) && game->cursor_locked &&
      !IsWindowFocused()) {
    game->pending_lock_request = true;
  }

  bool click_retrying_lock = !game->show_debug_menu && !game_is_paused(game) &&
                             game->pending_lock_request &&
                             (left_click_pressed || right_click_pressed);
  if (click_retrying_lock) {
    consumed_relock_click = true;
  }

  if (game->pending_lock_request && !game->show_debug_menu && !game_is_paused(game)) {
    if (IsWindowFocused() || click_retrying_lock) {
      set_cursor_locked(game, true);
    }

    if (IsWindowFocused()) {
      game->pending_lock_request = false;
    }
  }

  float hotbar_scroll = (!game->show_debug_menu && !game_is_paused(game) && game->cursor_locked)
                            ? input->mouse_wheel_delta
                            : 0.0f;
  Player_ApplyHotbarScroll(&game->player, hotbar_scroll);

  Profiler_BeginSection("Network");
  game_process_network(game);
  bool gameplay_enabled =
      (!game->show_debug_menu && !game_is_paused(game) && game->cursor_locked && !consumed_relock_click);
  game_send_input(game, input, gameplay_enabled);
  game->network_tick_counter++;
  Profiler_EndSection();

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
