#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "constants.h"
#include "net/net.h"
#include "server/server_core.h"

static volatile sig_atomic_t g_server_running = 1;

static void on_signal(int signum) {
  (void)signum;
  g_server_running = 0;
}

static void print_usage(const char *exe) {
  printf("Usage: %s [--bind <ip|*>] [--port <u16>] [--seed <int64>] [--render-distance <chunks>] "
         "[--max-clients <n>]\n",
         exe);
}

static int64_t random_seed(void) {
  int64_t t = (int64_t)time(NULL);
  int64_t c = (int64_t)clock();
  return t ^ (c << 21) ^ 0x9E3779B97F4A7C15LL;
}

static double now_seconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

int main(int argc, char **argv) {
  ServerConfig config = {
      .seed = random_seed(),
      .render_distance = DEFAULT_RENDER_DISTANCE,
      .tick_rate = (int)GAME_TICK_RATE,
      .max_clients = 1,
      .port = 25565,
      .mode = SERVER_MODE_DEDICATED,
  };
  NetEndpoint *net = NULL;
  ServerCore server;
  bool seeded = false;

  strncpy(config.bind, "*", sizeof(config.bind) - 1);

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--bind") == 0) {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return 1;
      }
      strncpy(config.bind, argv[++i], sizeof(config.bind) - 1);
      config.bind[sizeof(config.bind) - 1] = '\0';
    } else if (strcmp(argv[i], "--port") == 0) {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return 1;
      }
      config.port = (uint16_t)atoi(argv[++i]);
    } else if (strcmp(argv[i], "--seed") == 0) {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return 1;
      }
      config.seed = strtoll(argv[++i], NULL, 10);
      seeded = true;
    } else if (strcmp(argv[i], "--render-distance") == 0) {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return 1;
      }
      config.render_distance = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--max-clients") == 0) {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return 1;
      }
      config.max_clients = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    } else {
      print_usage(argv[0]);
      return 1;
    }
  }

  if (!seeded) {
    config.seed = random_seed();
  }

  printf("rlVoxelServer seed: %" PRId64 "\n", config.seed);
  printf("Listening on %s:%u\n", config.bind, (unsigned int)config.port);

  net = Net_Listen(config.bind, config.port, (size_t)config.max_clients);
  if (net == NULL) {
    fprintf(stderr, "Failed to initialize ENet server transport.\n");
    return 1;
  }

  if (!ServerCore_Init(&server, &config, net, true)) {
    fprintf(stderr, "Failed to initialize server core.\n");
    Net_Destroy(net);
    return 1;
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  double tick_dt = 1.0 / (double)config.tick_rate;
  double next_tick = now_seconds();

  while (g_server_running && ServerCore_IsRunning(&server)) {
    double now = now_seconds();
    if (now >= next_tick) {
      ServerCore_Tick(&server);
      next_tick += tick_dt;
      if (now - next_tick > 0.5) {
        next_tick = now + tick_dt;
      }
    } else {
      struct timespec sleep_for = {.tv_sec = 0, .tv_nsec = 1000000L};
      nanosleep(&sleep_for, NULL);
    }
  }

  ServerCore_Stop(&server);
  ServerCore_Shutdown(&server);
  return 0;
}
