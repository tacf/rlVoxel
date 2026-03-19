#include "telemetry.h"
#include <float.h>
#include <math.h>
#include <raylib.h>
#include <rlgl.h>
#include <stdio.h>
#include <string.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/utsname.h>
#endif

static Telemetry g_telemetry = {0};

static void copy_text(char *dst, size_t dst_size, const char *src) {
  if (dst == NULL || dst_size == 0) {
    return;
  }

  if (src == NULL) {
    dst[0] = '\0';
    return;
  }

  strncpy(dst, src, dst_size - 1);
  dst[dst_size - 1] = '\0';
}

static void collect_cpu_info(SystemInfo *info) {
#if defined(_WIN32)
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  info->cpu_cores = sysinfo.dwNumberOfProcessors;
  copy_text(info->cpu_name, sizeof(info->cpu_name), TextFormat("Windows CPU"));
#elif defined(__linux__)
  info->cpu_cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
  FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
  if (cpuinfo) {
    char line[256];
    while (fgets(line, sizeof(line), cpuinfo)) {
      if (strncmp(line, "model name", 10) == 0) {
        char *colon = strchr(line, ':');
        if (colon) {
          colon += 2; // Skip ": "
          size_t len = strlen(colon);
          if (len > 0 && colon[len - 1] == '\n') {
            colon[len - 1] = '\0';
          }
          copy_text(info->cpu_name, sizeof(info->cpu_name), TextFormat("%s", colon));
          break;
        }
      }
    }
    fclose(cpuinfo);
  }
  if (info->cpu_name[0] == '\0') {
    copy_text(info->cpu_name, sizeof(info->cpu_name), TextFormat("Linux CPU"));
  }
#elif defined(__APPLE__)
  size_t len = sizeof(info->cpu_cores);
  sysctlbyname("hw.ncpu", &info->cpu_cores, &len, NULL, 0);

  char brand[128];
  len = sizeof(brand);
  if (sysctlbyname("machdep.cpu.brand_string", brand, &len, NULL, 0) == 0) {
    copy_text(info->cpu_name, sizeof(info->cpu_name), TextFormat("%s", brand));
  } else {
    copy_text(info->cpu_name, sizeof(info->cpu_name), TextFormat("Apple CPU"));
  }
#else
  copy_text(info->cpu_name, sizeof(info->cpu_name), TextFormat("Unknown CPU"));
  info->cpu_cores = 1;
#endif
}

static void collect_gpu_info(SystemInfo *info) {
  copy_text(info->gpu_name, sizeof(info->gpu_name), TextFormat("OpenGL GPU"));
  copy_text(info->gpu_renderer, sizeof(info->gpu_renderer), TextFormat("Renderer"));
  copy_text(info->opengl_version, sizeof(info->opengl_version), TextFormat("OpenGL 3.3+"));
  copy_text(info->glsl_version, sizeof(info->glsl_version), TextFormat("GLSL 330+"));

  info->gpu_memory_mb = -1;
}

static void collect_os_info(SystemInfo *info) {
#if defined(_WIN32)
  copy_text(info->os_description, sizeof(info->os_description), TextFormat("Windows"));
#elif defined(__linux__)
  FILE *os_release = fopen("/etc/os-release", "r");
  if (os_release) {
    char line[256];
    while (fgets(line, sizeof(line), os_release)) {
      if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
        char *start = strchr(line, '"');
        if (start) {
          start++;
          char *end = strchr(start, '"');
          if (end) {
            *end = '\0';
            copy_text(info->os_description, sizeof(info->os_description), TextFormat("%s", start));
            fclose(os_release);
            return;
          }
        }
      }
    }
    fclose(os_release);
  }
  copy_text(info->os_description, sizeof(info->os_description), TextFormat("Linux"));
#elif defined(__APPLE__)
  struct utsname uts;
  if (uname(&uts) == 0) {
    copy_text(info->os_description, sizeof(info->os_description),
              TextFormat("macOS %s", uts.release));
  } else {
    copy_text(info->os_description, sizeof(info->os_description), TextFormat("macOS"));
  }
#else
  copy_text(info->os_description, sizeof(info->os_description), TextFormat("Unknown OS"));
#endif
}

void Telemetry_Init(void) {
  memset(&g_telemetry, 0, sizeof(Telemetry));

  collect_cpu_info(&g_telemetry.system_info);
  collect_gpu_info(&g_telemetry.system_info);
  collect_os_info(&g_telemetry.system_info);

  for (int i = 0; i < TELEMETRY_FPS_HISTORY_SIZE; i++) {
    g_telemetry.fps_history[i] = 0.0;
  }
  g_telemetry.fps_history_index = 0;

  g_telemetry.current_fps = 0.0;
  g_telemetry.avg_fps = 0.0;
  g_telemetry.min_fps = 0.0;
  g_telemetry.max_fps = 0.0;
  g_telemetry.frame_time_ms = 0.0;
}

void Telemetry_Shutdown(void) { memset(&g_telemetry, 0, sizeof(Telemetry)); }

void Telemetry_Update(float dt) {
  if (dt <= 0.0f || isnan(dt) || isinf(dt)) {
    dt = 1.0f / 60.0f;
  }

  g_telemetry.frame_time_ms = dt * 1000.0;
  g_telemetry.current_fps = 1.0 / dt;

  g_telemetry.fps_history[g_telemetry.fps_history_index] = g_telemetry.current_fps;
  g_telemetry.fps_history_index = (g_telemetry.fps_history_index + 1) % TELEMETRY_FPS_HISTORY_SIZE;

  double sum = 0.0;
  g_telemetry.min_fps = DBL_MAX;
  g_telemetry.max_fps = 0.0;
  int valid_samples = 0;

  for (int i = 0; i < TELEMETRY_FPS_HISTORY_SIZE; i++) {
    double fps = g_telemetry.fps_history[i];
    if (fps > 0.0) {
      sum += fps;
      valid_samples++;
      if (fps < g_telemetry.min_fps) {
        g_telemetry.min_fps = fps;
      }
      if (fps > g_telemetry.max_fps) {
        g_telemetry.max_fps = fps;
      }
    }
  }

  if (valid_samples > 0) {
    g_telemetry.avg_fps = sum / valid_samples;
  } else {
    g_telemetry.avg_fps = 0.0;
    g_telemetry.min_fps = 0.0;
    g_telemetry.max_fps = 0.0;
  }
}

void Telemetry_DrawWindow(void) {
  if (!igBegin("System Telemetry", NULL, 0)) {
    igEnd();
    return;
  }

  igSeparatorText("System Information");

  igText("CPU: %s (%d cores)", g_telemetry.system_info.cpu_name, g_telemetry.system_info.cpu_cores);
  igText("GPU: %s", g_telemetry.system_info.gpu_name);
  igText("Renderer: %s", g_telemetry.system_info.gpu_renderer);
  igText("OS: %s", g_telemetry.system_info.os_description);
  igText("OpenGL: %s", g_telemetry.system_info.opengl_version);
  igText("GLSL: %s", g_telemetry.system_info.glsl_version);

  if (g_telemetry.system_info.gpu_memory_mb >= 0) {
    igText("GPU Memory: %d MB", g_telemetry.system_info.gpu_memory_mb);
  } else {
    igText("GPU Memory: Unknown");
  }

  igSpacing();
  igSeparatorText("Frame Statistics");

  igText("Current FPS: %.1f", g_telemetry.current_fps);
  igText("Average FPS: %.1f", g_telemetry.avg_fps);
  igText("Min FPS: %.1f", g_telemetry.min_fps);
  igText("Max FPS: %.1f", g_telemetry.max_fps);
  igText("Frame Time: %.3f ms", g_telemetry.frame_time_ms);

  igEnd();
}

const Telemetry *Telemetry_Get(void) { return &g_telemetry; }
