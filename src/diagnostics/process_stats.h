#ifndef PROCESS_STATS_H
#define PROCESS_STATS_H

#include <stdbool.h>

typedef struct ProcessStats {
  long memory_mb;
  int thread_count;
  bool has_memory_mb;
  bool has_thread_count;
} ProcessStats;

void ProcessStats_Query(ProcessStats *stats);

#endif
