#include "gfx/shader_paths.h"

#include <raylib.h>
#include <stdio.h>
#include <string.h>

static const char *shader_primary_folder(void) {
#if defined(GRAPHICS_API_OPENGL_21)
  return "120";
#elif defined(GRAPHICS_API_OPENGL_ES2)
  return "100";
#elif defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_43)
  return "330";
#elif defined(PLATFORM_DESKTOP)
  return "330";
#else
  return "100";
#endif
}

void ShaderPaths_Resolve(char *dst, size_t dst_size, const char *relative_path) {
  if (dst == NULL || dst_size == 0 || relative_path == NULL) {
    return;
  }

  const char *folders[] = {
      shader_primary_folder(),
      "330",
      "120",
      "100",
  };
  const size_t folder_count = sizeof(folders) / sizeof(folders[0]);

  char versioned_path[256];
  for (size_t i = 0; i < folder_count; i++) {
    if (i > 0 && strcmp(folders[i], folders[i - 1]) == 0) {
      continue;
    }

    snprintf(versioned_path, sizeof(versioned_path), "assets/shaders/%s/%s", folders[i],
             relative_path);
    if (FileExists(versioned_path)) {
      snprintf(dst, dst_size, "%s", versioned_path);
      return;
    }
  }

  snprintf(dst, dst_size, "assets/shaders/%s", relative_path);
}
