#include "profiler.h"

#include <raylib.h>
#include <stdio.h>
#include <string.h>

static Profiler g_profiler = {0};

static int find_or_create_section(const char *name, int parent_index);
static int find_section(const char *name, int parent_index);

void Profiler_Init(void) {
  memset(&g_profiler, 0, sizeof(Profiler));

  g_profiler.enabled = true;

  for (int i = 0; i < PROFILER_MAX_SECTIONS; i++) {
    g_profiler.sections[i].parent_index = -1;
  }

  g_profiler.stack_depth = 0;
  g_profiler.current_section_index = -1;
  g_profiler.history_write_index = 0;
}

void Profiler_Shutdown(void) { memset(&g_profiler, 0, sizeof(Profiler)); }

void Profiler_BeginFrame(void) {
  if (!g_profiler.enabled) {
    return;
  }

  g_profiler.frame_start_time = GetTime();

  for (int i = 0; i < g_profiler.section_count; i++) {
    g_profiler.sections[i].current_frame_ms = 0.0;
    g_profiler.sections[i].current_frame_hits = 0;
  }
}

void Profiler_EndFrame(void) {
  if (!g_profiler.enabled) {
    return;
  }

  double frame_end_time = GetTime();
  double frame_time_sec = frame_end_time - g_profiler.frame_start_time;
  g_profiler.last_frame_time_ms = frame_time_sec * 1000.0;

  if (g_profiler.stack_depth != 0) {
    fprintf(stderr,
            "Profiler warning: %d sections/groups remained on stack at "
            "frame end; resetting stack.\n",
            g_profiler.stack_depth);
  }

  g_profiler.stack_depth = 0;
  g_profiler.current_section_index = -1;

  for (int i = 0; i < g_profiler.section_count; i++) {
    g_profiler.sections[i].is_active = false;
  }
}

void Profiler_CaptureFrame(void) {
  if (!g_profiler.enabled) {
    return;
  }

  double current_time = GetTime();
  static double last_capture_time = 0.0;

  if (last_capture_time == 0.0) {
    last_capture_time = current_time;
  }

  double delta_time = current_time - last_capture_time;
  last_capture_time = current_time;

  g_profiler.max_update_timer += delta_time;

  if (g_profiler.max_update_timer >= 1.0) {
    g_profiler.max_update_timer = 0.0;

    for (int i = 0; i < g_profiler.section_count; i++) {
      ProfilerSection *section = &g_profiler.sections[i];
      section->previous_period_max_ms = section->current_period_max_ms;
      section->current_period_max_ms = 0.0;
      section->max_time_ms = section->previous_period_max_ms;
    }
  }

  int write_index = g_profiler.history_write_index;
  int next_index = (write_index + 1) % PROFILER_HISTORY_SIZE;

  for (int i = 0; i < g_profiler.section_count; i++) {
    ProfilerSection *section = &g_profiler.sections[i];
    double frame_sample_ms = section->current_frame_ms;

    section->history[write_index] = frame_sample_ms;
    section->history_index = next_index;
    section->last_time_ms = frame_sample_ms;

    if (frame_sample_ms > section->current_period_max_ms) {
      section->current_period_max_ms = frame_sample_ms;
    }
    section->max_time_ms = (section->current_period_max_ms > section->previous_period_max_ms)
                               ? section->current_period_max_ms
                               : section->previous_period_max_ms;

    section->total_sampled_frames++;
    if (section->current_frame_hits > 0) {
      section->total_hit_frames++;
    }
  }

  g_profiler.history_write_index = next_index;
}

static int find_section(const char *name, int parent_index) {
  for (int i = 0; i < g_profiler.section_count; i++) {
    const ProfilerSection *section = &g_profiler.sections[i];
    if ((section->parent_index == parent_index) && (strcmp(section->name, name) == 0)) {
      return i;
    }
  }
  return -1;
}

static int find_or_create_section(const char *name, int parent_index) {
  if (!g_profiler.enabled) {
    return -1;
  }

  int index = find_section(name, parent_index);
  if (index >= 0) {
    return index;
  }

  if (g_profiler.section_count >= PROFILER_MAX_SECTIONS) {
    fprintf(stderr, "Profiler error: Section limit exceeded (%d sections)\n",
            PROFILER_MAX_SECTIONS);
    return -1;
  }

  index = g_profiler.section_count++;
  ProfilerSection *section = &g_profiler.sections[index];
  memset(section, 0, sizeof(*section));

  strncpy(section->name, name, sizeof(section->name) - 1);
  section->name[sizeof(section->name) - 1] = '\0';
  section->parent_index = parent_index;
  section->depth = (parent_index >= 0) ? g_profiler.sections[parent_index].depth + 1 : 0;
  section->history_index = g_profiler.history_write_index;

  return index;
}

void Profiler_BeginSection(const char *name) {
  if (!g_profiler.enabled) {
    return;
  }

  int parent_index =
      (g_profiler.stack_depth > 0) ? g_profiler.section_stack[g_profiler.stack_depth - 1] : -1;

  int section_index = find_or_create_section(name, parent_index);
  if (section_index < 0) {
    return;
  }

  ProfilerSection *section = &g_profiler.sections[section_index];
  section->start_time = GetTime();
  section->is_active = true;

  if (g_profiler.stack_depth < PROFILER_MAX_DEPTH) {
    g_profiler.section_stack[g_profiler.stack_depth] = section_index;
    g_profiler.stack_depth++;
    g_profiler.current_section_index = section_index;
  } else {
    fprintf(stderr, "Profiler error: Stack overflow (max depth: %d)\n", PROFILER_MAX_DEPTH);
    section->is_active = false;
  }
}

void Profiler_EndSection(void) {
  if (!g_profiler.enabled) {
    return;
  }

  if (g_profiler.stack_depth <= 0) {
    fprintf(stderr, "Profiler error: EndSection called without matching BeginSection\n");
    return;
  }

  g_profiler.stack_depth--;
  int section_index = g_profiler.section_stack[g_profiler.stack_depth];
  ProfilerSection *section = &g_profiler.sections[section_index];

  if (!section->is_active) {
    fprintf(stderr, "Profiler error: Section '%s' is not active\n", section->name);

    if (g_profiler.stack_depth > 0) {
      g_profiler.current_section_index = g_profiler.section_stack[g_profiler.stack_depth - 1];
    } else {
      g_profiler.current_section_index = -1;
    }
    return;
  }

  double end_time = GetTime();
  double elapsed_ms = (end_time - section->start_time) * 1000.0;
  section->is_active = false;

  section->current_frame_ms += elapsed_ms;
  section->current_frame_hits++;

  if (section->avg_time_ms == 0.0) {
    section->avg_time_ms = elapsed_ms;
  } else {
    section->avg_time_ms = 0.95 * section->avg_time_ms + 0.05 * elapsed_ms;
  }

  if (g_profiler.stack_depth > 0) {
    g_profiler.current_section_index = g_profiler.section_stack[g_profiler.stack_depth - 1];
  } else {
    g_profiler.current_section_index = -1;
  }
}

void Profiler_PushGroup(const char *name) {
  if (!g_profiler.enabled) {
    return;
  }

  int parent_index =
      (g_profiler.stack_depth > 0) ? g_profiler.section_stack[g_profiler.stack_depth - 1] : -1;
  int section_index = find_or_create_section(name, parent_index);
  if (section_index < 0) {
    return;
  }

  if (g_profiler.stack_depth < PROFILER_MAX_DEPTH) {
    g_profiler.section_stack[g_profiler.stack_depth] = section_index;
    g_profiler.stack_depth++;
    g_profiler.current_section_index = section_index;
  } else {
    fprintf(stderr, "Profiler error: Stack overflow (max depth: %d)\n", PROFILER_MAX_DEPTH);
  }
}

void Profiler_PopGroup(void) {
  if (!g_profiler.enabled) {
    return;
  }

  if (g_profiler.stack_depth <= 0) {
    fprintf(stderr, "Profiler error: PopGroup called without matching PushGroup\n");
    return;
  }

  g_profiler.stack_depth--;

  if (g_profiler.stack_depth > 0) {
    g_profiler.current_section_index = g_profiler.section_stack[g_profiler.stack_depth - 1];
  } else {
    g_profiler.current_section_index = -1;
  }
}

void Profiler_SetEnabled(bool enabled) { g_profiler.enabled = enabled; }

bool Profiler_IsEnabled(void) { return g_profiler.enabled; }

const Profiler *Profiler_Get(void) { return &g_profiler; }

int Profiler_GetSectionCount(void) { return g_profiler.section_count; }

const ProfilerSection *Profiler_GetSection(int index) {
  if (index < 0 || index >= g_profiler.section_count) {
    return NULL;
  }
  return &g_profiler.sections[index];
}
