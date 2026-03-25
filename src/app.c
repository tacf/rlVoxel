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

static void print_usage(const char *exe) {
  printf("Usage: %s [--seed <int64>] [--render-distance <chunks>] [--connect <host[:port]>]\n",
         exe);
}

int main(int argc, char **argv) {
  bool has_seed = false;
  bool has_connect = false;
  int64_t seed = 0;
  int render_distance = DEFAULT_RENDER_DISTANCE;
  char connect_host[128] = {0};
  uint16_t connect_port = 56552;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--seed") == 0) {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return 1;
      }
      seed = strtoll(argv[++i], NULL, 10);
      has_seed = true;
    } else if (strcmp(argv[i], "--render-distance") == 0) {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return 1;
      }
      render_distance = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--connect") == 0) {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return 1;
      }
      const char *value = argv[++i];
      const char *colon = strchr(value, ':');
      if (colon != NULL) {
        size_t host_len = (size_t)(colon - value);
        if (host_len >= sizeof(connect_host)) {
          host_len = sizeof(connect_host) - 1;
        }
        memcpy(connect_host, value, host_len);
        connect_host[host_len] = '\0';
        connect_port = (uint16_t)atoi(colon + 1);
      } else {
        strncpy(connect_host, value, sizeof(connect_host) - 1);
        connect_host[sizeof(connect_host) - 1] = '\0';
      }
      has_connect = true;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    } else {
      print_usage(argv[0]);
      return 1;
    }
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

  Game game;
  if (!Game_Init(&game, seed, render_distance, has_connect ? connect_host : NULL, connect_port)) {
    fprintf(stderr, "Failed to initialize game resources.\n");
    CloseWindow();
    return 1;
  }

  double previous_time = GetTime();
  double accumulator = 0.0;
  GameInputSnapshot pending_input = {0};

  while (!WindowShouldClose()) {
    double current_time = GetTime();
    double frame_dt = current_time - previous_time;
    previous_time = current_time;

    if (frame_dt < 0.0) {
      frame_dt = 0.0;
    } else if (frame_dt > GAME_MAX_FRAME_DELTA) {
      frame_dt = GAME_MAX_FRAME_DELTA;
    }

    accumulator += frame_dt;

    GameInputSnapshot frame_input = {0};
    Game_CaptureFrameInput(&frame_input);
    Game_MergeFrameInput(&pending_input, &frame_input);

    Profiler_BeginFrame();
    Telemetry_Update((float)frame_dt);

    int ticks_this_frame = 0;
    while (accumulator >= GAME_TICK_DT && ticks_this_frame < GAME_MAX_TICKS_PER_FRAME) {
      Game_Tick(&game, &pending_input, (float)GAME_TICK_DT);
      if (WindowShouldClose()) {
        break;
      }
      Game_ClearTickEdgeInput(&pending_input);
      accumulator -= GAME_TICK_DT;
      ticks_this_frame++;
    }

    if (WindowShouldClose()) {
      break;
    }

    if (ticks_this_frame == GAME_MAX_TICKS_PER_FRAME && accumulator >= GAME_TICK_DT) {
      accumulator = GAME_TICK_DT;
    }

    float alpha = (float)(accumulator / GAME_TICK_DT);
    if (alpha < 0.0f) {
      alpha = 0.0f;
    } else if (alpha > 1.0f) {
      alpha = 1.0f;
    }

    Game_Draw(&game, alpha);

    Profiler_EndFrame();
    Profiler_CaptureFrame();
  }

  Game_Shutdown(&game);
  CloseWindow();
  return 0;
}
