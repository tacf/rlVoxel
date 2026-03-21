#ifndef SHADER_PATHS_H
#define SHADER_PATHS_H

#include <stddef.h>

void ShaderPaths_Resolve(char *dst, size_t dst_size, const char *relative_path);

#endif
