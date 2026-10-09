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

    if (strcmp(flag, "--bind") == 0) {
      strncpy(config.bind, value, sizeof(config.bind) - 1);
    } else if (strcmp(flag, "--port") == 0) {
      config.port = (uint16_t)atoi(value);
    } else if (strcmp(flag, "--seed") == 0) {
      config.seed = strtoll(value, NULL, 10);
      seeded = true;
    } else if (strcmp(flag, "--render-distance") == 0) {
      config.render_distance = atoi(value);
    } else if (strcmp(flag, "--max-clients") == 0) {
      config.max_clients = atoi(value);
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

  if (!ServerCore_StartThread(&server)) {
    fprintf(stderr, "Failed to start server thread.\n");
    ServerCore_Shutdown(&server);
    return 1;
  }

  while (g_server_running) {
    struct timespec sleep_for = {.tv_sec = 0, .tv_nsec = 100000000L};
    nanosleep(&sleep_for, NULL);
  }

  ServerCore_Stop(&server);
  ServerCore_Shutdown(&server);
  return 0;
}
