#include "process_stats.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <psapi.h>
#include <tlhelp32.h>
#include <windows.h>
#elif defined(__linux__)
#include <dirent.h>
#endif

static long query_memory_usage_mb(void) {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    return (long)(pmc.WorkingSetSize / (1024 * 1024));
  }
  return -1;
#elif defined(__linux__)
  FILE *fp = fopen("/proc/self/status", "r");
  if (fp == NULL) {
    return -1;
  }

  char line[256];
  long vmrss_kb = -1;

  while (fgets(line, sizeof(line), fp) != NULL) {
    if (strncmp(line, "VmRSS:", 6) == 0) {
      if (sscanf(line + 6, "%ld", &vmrss_kb) != 1) {
        vmrss_kb = -1;
      }
      break;
    }
  }

  fclose(fp);

  if (vmrss_kb < 0) {
    return -1;
  }
  return vmrss_kb / 1024;
#else
  return -1;
#endif
}

static int query_thread_count(void) {
#if defined(_WIN32)
  DWORD process_id = GetCurrentProcessId();
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return -1;
  }

  THREADENTRY32 te;
  te.dwSize = sizeof(te);

  int count = 0;
  if (Thread32First(snapshot, &te)) {
    do {
      if (te.th32OwnerProcessID == process_id) {
        count++;
      }
    } while (Thread32Next(snapshot, &te));
  }

  CloseHandle(snapshot);
  return count;
#elif defined(__linux__)
  DIR *dir = opendir("/proc/self/task");
  if (dir == NULL) {
    return -1;
  }

  int count = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] != '.') {
      count++;
    }
  }

  closedir(dir);
  return count;
#else
  return -1;
#endif
}

void ProcessStats_Query(ProcessStats *stats) {
  if (stats == NULL) {
    return;
  }

  memset(stats, 0, sizeof(*stats));
  stats->memory_mb = -1;
  stats->thread_count = -1;

  long memory_mb = query_memory_usage_mb();
  int thread_count = query_thread_count();

  if (memory_mb >= 0) {
    stats->memory_mb = memory_mb;
    stats->has_memory_mb = true;
  }

  if (thread_count >= 0) {
    stats->thread_count = thread_count;
    stats->has_thread_count = true;
  }
}
