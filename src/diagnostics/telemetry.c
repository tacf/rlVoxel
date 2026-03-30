#include "telemetry.h"
#include "process_stats.h"
#include <float.h>
#include <math.h>
#include <raylib.h>
#include <rlgl.h>
#include <stdio.h>
#include <string.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>

#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C

#define GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX 0x9047
#define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX 0x9048
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX 0x9049
#define GL_TEXTURE_FREE_MEMORY_ATI 0x87FC

typedef const unsigned char *(*GLGetStringFunc)(unsigned int);
typedef void (*GLGetIntegervFunc)(unsigned int, int *);

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
  GLGetStringFunc glGetStringPtr = (GLGetStringFunc)rlGetProcAddress("glGetString");
  GLGetIntegervFunc glGetIntegervPtr = (GLGetIntegervFunc)rlGetProcAddress("glGetIntegerv");

  if (glGetStringPtr) {
    const char *vendor = glGetStringPtr(GL_VENDOR);
    const char *renderer = glGetStringPtr(GL_RENDERER);
    const char *version = glGetStringPtr(GL_VERSION);
    const char *glsl = glGetStringPtr(GL_SHADING_LANGUAGE_VERSION);

    if (vendor && renderer) {
      copy_text(info->gpu_name, sizeof(info->gpu_name), TextFormat("%s %s", vendor, renderer));
    } else if (renderer) {
      copy_text(info->gpu_name, sizeof(info->gpu_name), TextFormat("%s", renderer));
    } else {
      copy_text(info->gpu_name, sizeof(info->gpu_name), TextFormat("Unknown GPU"));
    }

    if (renderer) {
      copy_text(info->gpu_renderer, sizeof(info->gpu_renderer), TextFormat("%s", renderer));
    } else {
      copy_text(info->gpu_renderer, sizeof(info->gpu_renderer), TextFormat("Unknown"));
    }

    if (version) {
      copy_text(info->opengl_version, sizeof(info->opengl_version), TextFormat("%s", version));
    } else {
      copy_text(info->opengl_version, sizeof(info->opengl_version), TextFormat("Unknown"));
    }

    if (glsl) {
      copy_text(info->glsl_version, sizeof(info->glsl_version), TextFormat("%s", glsl));
    } else {
      copy_text(info->glsl_version, sizeof(info->glsl_version), TextFormat("Unknown"));
    }
  } else {
    copy_text(info->gpu_name, sizeof(info->gpu_name), TextFormat("Unknown GPU"));
    copy_text(info->gpu_renderer, sizeof(info->gpu_renderer), TextFormat("Unknown"));
    copy_text(info->opengl_version, sizeof(info->opengl_version), TextFormat("Unknown"));
    copy_text(info->glsl_version, sizeof(info->glsl_version), TextFormat("Unknown"));
  }

  info->gpu_memory_mb = -1;
  if (glGetIntegervPtr) {
    int dedicated_mb = 0;
    glGetIntegervPtr(GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &dedicated_mb);
    if (dedicated_mb > 0) {
      info->gpu_memory_mb = dedicated_mb;
    } else {
      int tex_free = 0;
      glGetIntegervPtr(GL_TEXTURE_FREE_MEMORY_ATI, &tex_free);
      if (tex_free > 0) {
        info->gpu_memory_mb = tex_free;
      }
    }
  }
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
  g_telemetry.process_memory_mb = -1;
  g_telemetry.process_thread_count = -1;
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

  ProcessStats proc_stats;
  ProcessStats_Query(&proc_stats);
  if (proc_stats.has_memory_mb) {
    g_telemetry.process_memory_mb = proc_stats.memory_mb;
  }
  if (proc_stats.has_thread_count) {
    g_telemetry.process_thread_count = proc_stats.thread_count;
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
  igSeparatorText("Process Memory");

  if (g_telemetry.process_memory_mb >= 0) {
    igText("RAM Usage: %ld MB", g_telemetry.process_memory_mb);
  } else {
    igText("RAM Usage: Unknown");
  }
  if (g_telemetry.process_thread_count >= 0) {
    igText("Threads: %d", g_telemetry.process_thread_count);
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
