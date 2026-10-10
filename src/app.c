#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "constants.h"
#include <raylib.h>

#include "game/game.h"
#include "game/game_input.h"
#include "profiling/profiler.h"
#include "diagnostics/telemetry.h"
#include "ui/font_atlas.h"
#include "ui/menu.h"
#include "ui/clay_render.h"

static void print_usage(const char *exe) {
  printf("Usage: %s [--seed <int64>] [--render-distance <chunks>] [--connect <host[:port]>] "
         "[--saves-dir <directory>] [--world <directory>]\n",
         exe);
}

int main(int argc, char **argv) {
  bool has_seed = false;
  bool has_connect = false;
  int64_t seed = 0;
  int render_distance = DEFAULT_RENDER_DISTANCE;
  char connect_host[128] = {0};
  uint16_t connect_port = 56552;
  const char *saves_root = "saves";
  const char *world_directory = NULL;

  for (int i = 1; i < argc; i++) {
    const char *flag = argv[i];

    if (strcmp(flag, "--help") == 0 || strcmp(flag, "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    }

    if (i + 1 >= argc) {
      print_usage(argv[0]);
      return 1;
    }
    const char *value = argv[++i];

    if (strcmp(flag, "--seed") == 0) {
      seed = strtoll(value, NULL, 10);
      has_seed = true;
    } else if (strcmp(flag, "--render-distance") == 0) {
      render_distance = atoi(value);
    } else if (strcmp(flag, "--connect") == 0) {
      const char *colon = strchr(value, ':');
      size_t host_len = (colon != NULL) ? (size_t)(colon - value) : strlen(value);
      if (host_len >= sizeof(connect_host)) {
        host_len = sizeof(connect_host) - 1;
      }
      memcpy(connect_host, value, host_len);
      connect_host[host_len] = '\0';
      if (colon != NULL) {
        connect_port = (uint16_t)atoi(colon + 1);
      }
      has_connect = true;
    } else if (strcmp(flag, "--saves-dir") == 0) {
      saves_root = value;
    } else if (strcmp(flag, "--world") == 0) {
      world_directory = value;
    } else {
      print_usage(argv[0]);
      return 1;
    }
  }

  if (strlen(saves_root) >= WORLD_SAVE_PATH_SIZE - 96 ||
      (world_directory && strlen(world_directory) >= WORLD_SAVE_PATH_SIZE) ||
      (has_connect && world_directory)) {
    fprintf(stderr, "Invalid save path or conflicting --connect and --world options.\n");
    return 1;
  }

  if (!has_seed) {
    int64_t t = (int64_t)time(NULL);
    int64_t c = (int64_t)clock();
    seed = t ^ (c << 21) ^ 0x9E3779B97F4A7C15LL;
  }

  printf("rl-voxel seed: %" PRId64 "\n", seed);

  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(1280, 720, "rl-voxel");
  if (!IsWindowReady()) {
    fprintf(stderr, "Failed to open a graphics window (display may be unavailable).\n");
    return 1;
  }
  SetTargetFPS(60);
  SetExitKey(KEY_NULL);
  SetWindowMinSize(800, 600);

  Font menu_font = FontAtlas_Load("assets/fonts/default.ttf", 16);
  if (!menu_font.texture.id) {
    fprintf(stderr, "Failed to load menu font.\n");
    CloseWindow();
    return 1;
  }
  SetTextureFilter(menu_font.texture, TEXTURE_FILTER_POINT);
  if (!ClayRender_Init(&menu_font)) {
    UnloadFont(menu_font);
    CloseWindow();
    return 1;
  }
  Menu menu;
  Menu_Init(&menu, saves_root, seed);
  Game game = {0};
  WorldSaveInfo selected_world = {0};
  bool playing = false;
  bool quit = false;
  int exit_code = 0;

  if (has_connect || world_directory) {
    bool ready = has_connect || WorldSave_ReadInfo(world_directory, &selected_world, menu.error);
    if (ready) {
      playing = Game_Init(&game, has_connect ? seed : selected_world.seed, render_distance,
                          has_connect ? connect_host : NULL, connect_port,
                          has_connect ? NULL : selected_world.directory);
      if (!playing)
        snprintf(menu.error, sizeof(menu.error), "%s",
                 game.save_error[0] ? game.save_error : "Could not start the game.");
    }
    if (!playing)
      EnableCursor();
  }

  double previous_time = GetTime();
  double accumulator = 0.0;
  GameInputSnapshot pending_input = {0};

  while (!WindowShouldClose() && !quit) {
    double current_time = GetTime();
    double frame_dt = current_time - previous_time;
    previous_time = current_time;

    if (frame_dt < 0.0) {
      frame_dt = 0.0;
    } else if (frame_dt > GAME_MAX_FRAME_DELTA) {
      frame_dt = GAME_MAX_FRAME_DELTA;
    }

    if (!playing) {
      BeginDrawing();
      MenuAction action = Menu_Draw(&menu, &menu_font, &selected_world);
      EndDrawing();
      if (action == MENU_QUIT)
        quit = true;
      if (action == MENU_START_WORLD) {
        BeginDrawing();
        Menu_DrawLoading(menu_font);
        EndDrawing();
        playing = Game_Init(&game, selected_world.seed, render_distance, NULL, 0,
                            selected_world.directory);
        if (!playing) {
          EnableCursor();
          snprintf(menu.error, sizeof(menu.error), "%s",
                   game.save_error[0] ? game.save_error : "Could not start the game.");
          menu.screen = MENU_WORLDS;
          Menu_RefreshWorlds(&menu);
        }
        accumulator = 0;
        pending_input = (GameInputSnapshot){0};
        previous_time = GetTime();
      }
      continue;
    }

    accumulator += frame_dt;

    GameInputSnapshot frame_input = {0};
    Game_CaptureFrameInput(&frame_input);
    Game_ApplyFrameLook(&game, &frame_input);
    Game_MergeFrameInput(&pending_input, &frame_input);

    Profiler_BeginFrame();
    Telemetry_Update((float)frame_dt);

    int ticks_this_frame = 0;
    while (accumulator >= GAME_TICK_DT && ticks_this_frame < GAME_MAX_TICKS_PER_FRAME) {
      Game_Tick(&game, &pending_input, (float)GAME_TICK_DT);
      if (WindowShouldClose() || game.quit_requested || game.return_to_menu_requested) {
        break;
      }
      Game_ClearTickEdgeInput(&pending_input);
      accumulator -= GAME_TICK_DT;
      ticks_this_frame++;
    }

    if (WindowShouldClose()) {
      break;
    }

    if (game.quit_requested || game.return_to_menu_requested) {
      bool quitting = game.quit_requested;
      if (Game_Save(&game)) {
        Game_Shutdown(&game);
        playing = false;
        quit = quitting;
        EnableCursor();
        menu.screen = MENU_WORLDS;
        menu.error[0] = 0;
        Menu_RefreshWorlds(&menu);
        accumulator = 0;
        pending_input = (GameInputSnapshot){0};
        previous_time = GetTime();
        continue;
      }
    }

    if (ticks_this_frame == GAME_MAX_TICKS_PER_FRAME && accumulator >= GAME_TICK_DT) {
      accumulator = GAME_TICK_DT;
    }

    Game_Draw(&game, (float)(accumulator / GAME_TICK_DT));

    Profiler_EndFrame();
    Profiler_CaptureFrame();
  }

  if (playing) {
    if (!Game_Save(&game)) {
      fprintf(stderr, "World save failed on exit: %s\n", game.save_error);
      exit_code = 1;
    }
    Game_Shutdown(&game);
  }
  Menu_Shutdown(&menu);
  ClayRender_Shutdown();
  UnloadFont(menu_font);
  CloseWindow();
  return exit_code;
}
