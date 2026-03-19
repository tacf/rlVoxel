#ifndef TELEMETRY_H
#define TELEMETRY_H

#define TELEMETRY_FPS_HISTORY_SIZE 600

typedef struct SystemInfo {
  char cpu_name[128];
  int cpu_cores;
  char gpu_name[128];
  char gpu_renderer[128];
  char os_description[128];
  char opengl_version[64];
  char glsl_version[64];
  int gpu_memory_mb;
} SystemInfo;

typedef struct Telemetry {
  SystemInfo system_info;

  double fps_history[TELEMETRY_FPS_HISTORY_SIZE];
  int fps_history_index;

  double current_fps;
  double avg_fps;
  double min_fps;
  double max_fps;
  double frame_time_ms;
} Telemetry;

void Telemetry_Init(void);
void Telemetry_Shutdown(void);

void Telemetry_Update(float dt);

void Telemetry_DrawWindow(void);

const Telemetry *Telemetry_Get(void);

#endif
