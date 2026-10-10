#ifndef RLVOXEL_GAME_BLOCK_BREAK_H
#define RLVOXEL_GAME_BLOCK_BREAK_H

#include "constants.h"

/* Shared by break-pulse input, local cracks, and authoritative damage recovery. */
#define BLOCK_BREAK_PULSE_TICKS 5u
#define BLOCK_BREAK_RECOVERY_DELAY_TICKS 10u
#define BLOCK_BREAK_RECOVERY_INTERVAL_TICKS 5u

static inline float BlockBreak_DurationSeconds(int durability, int server_tick_rate) {
  if (durability <= 0)
    return 0.0f;
  if (server_tick_rate <= 0)
    server_tick_rate = (int)GAME_TICK_RATE;
  /* The first hit is immediate; subsequent hits arrive one pulse apart.
   * Include one tick for the final hit to be processed and replicated. */
  return (float)(durability - 1) * BLOCK_BREAK_PULSE_TICKS * GAME_TICK_DT +
         1.0f / (float)server_tick_rate;
}

#endif
