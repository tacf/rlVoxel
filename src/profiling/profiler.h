#ifndef PROFILER_H
#define PROFILER_H

#include <stdbool.h>

#define PROFILER_MAX_DEPTH 16
#define PROFILER_MAX_SECTIONS 64
#define PROFILER_HISTORY_SIZE 300

typedef struct ProfilerSection {
  char name[64];
  int parent_index;
  int depth;

  double last_time_ms;
  double avg_time_ms;
  double max_time_ms;
  double current_period_max_ms;
  double previous_period_max_ms;

  double history[PROFILER_HISTORY_SIZE];
  int history_index;

  double current_frame_ms;
  int current_frame_hits;
  unsigned int total_sampled_frames;
  unsigned int total_hit_frames;

  double start_time;
  bool is_active;
} ProfilerSection;

typedef struct Profiler {
  ProfilerSection sections[PROFILER_MAX_SECTIONS];
  int section_count;

  int current_section_index;
  int section_stack[PROFILER_MAX_DEPTH];
  int stack_depth;

  bool enabled;

  double frame_start_time;
  double last_frame_time_ms;

  double max_update_timer;
  int history_write_index;
} Profiler;

void Profiler_Init(void);
void Profiler_Shutdown(void);

void Profiler_BeginFrame(void);
void Profiler_EndFrame(void);
void Profiler_CaptureFrame(void);

void Profiler_BeginSection(const char *name);
void Profiler_EndSection(void);

void Profiler_PushGroup(const char *name);
void Profiler_PopGroup(void);

void Profiler_SetEnabled(bool enabled);
bool Profiler_IsEnabled(void);

const Profiler *Profiler_Get(void);
int Profiler_GetSectionCount(void);
const ProfilerSection *Profiler_GetSection(int index);

#endif
