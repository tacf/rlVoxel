#include "gfx/renderer.h"
#include "gfx/shader_paths.h"

#include "profiling/profiler.h"
#include "raylib.h"
#include <stddef.h>

static bool renderer_create_target(Renderer *renderer, int width, int height) {
  if (renderer == NULL || width <= 0 || height <= 0) {
    return false;
  }

  renderer->target = LoadRenderTexture(width, height);
  if (renderer->target.id != 0) {
    int filter = renderer->pixelate_enabled ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR;
    SetTextureFilter(renderer->target.texture, filter);
  }
  return renderer->target.id != 0;
}

static void renderer_update_resolution_uniform(Renderer *renderer) {
  if (renderer == NULL || renderer->fxaa.id == 0 || renderer->fxaa_resolution_loc < 0) {
    return;
  }

  Vector2 resolution = {(float)renderer->target.texture.width,
                        (float)renderer->target.texture.height};
  SetShaderValue(renderer->fxaa, renderer->fxaa_resolution_loc, &resolution, SHADER_UNIFORM_VEC2);
}

static bool renderer_ensure_target_size(Renderer *renderer) {
  if (renderer == NULL) {
    return false;
  }

  int width = GetScreenWidth();
  int height = GetScreenHeight();
  int downscale = (renderer->pixelate_enabled && renderer->pixelate_downscale > 1)
                      ? renderer->pixelate_downscale
                      : 1;

  width = (width + downscale - 1) / downscale;
  height = (height + downscale - 1) / downscale;
  if (width <= 0 || height <= 0) {
    return false;
  }

  if (renderer->target.id != 0 && renderer->target.texture.width == width &&
      renderer->target.texture.height == height) {
    return true;
  }

  if (renderer->target.id != 0) {
    UnloadRenderTexture(renderer->target);
    renderer->target = (RenderTexture2D){0};
  }

  if (!renderer_create_target(renderer, width, height)) {
    return false;
  }

  renderer_update_resolution_uniform(renderer);
  return true;
}

bool Renderer_Init(Renderer *renderer) {
  if (renderer == NULL) {
    return false;
  }

  *renderer = (Renderer){0};

  char fxaa_path[256];
  ShaderPaths_Resolve(fxaa_path, sizeof(fxaa_path), "postprocessing/fxaa.fs");
  renderer->fxaa = LoadShader(0, fxaa_path);
  if (renderer->fxaa.id == 0) {
    return false;
  }

  renderer->fxaa_resolution_loc = GetShaderLocation(renderer->fxaa, "resolution");

  renderer->pixelate_enabled = false;
  renderer->fxaa_enabled = true;
  renderer->pixelate_downscale = 4;

  if (!renderer_ensure_target_size(renderer)) {
    Renderer_Shutdown(renderer);
    return false;
  }

  return true;
}

void Renderer_Shutdown(Renderer *renderer) {
  if (renderer == NULL) {
    return;
  }

  if (renderer->target.id != 0) {
    UnloadRenderTexture(renderer->target);
  }
  if (renderer->fxaa.id != 0) {
    UnloadShader(renderer->fxaa);
  }

  *renderer = (Renderer){0};
}

void Renderer_SetPixelateEnabled(Renderer *renderer, bool enabled) {
  if (renderer == NULL) {
    return;
  }
  if (renderer->pixelate_enabled == enabled) {
    return;
  }
  renderer->pixelate_enabled = enabled;
  if (renderer->target.id != 0) {
    UnloadRenderTexture(renderer->target);
    renderer->target = (RenderTexture2D){0};
  }
}

bool Renderer_GetPixelateEnabled(const Renderer *renderer) {
  if (renderer == NULL) {
    return false;
  }
  return renderer->pixelate_enabled;
}

void Renderer_DrawFrame(Renderer *renderer, const Camera3D *camera, Color clear_color,
                        const RendererFrameCallbacks *callbacks) {
  if (renderer == NULL || camera == NULL || callbacks == NULL || callbacks->draw_world == NULL) {
    return;
  }

  if (!renderer_ensure_target_size(renderer)) {
    return;
  }

  Profiler_BeginSection("WorldRender");
  BeginTextureMode(renderer->target);
  ClearBackground(clear_color);
  BeginMode3D(*camera);
  callbacks->draw_world(callbacks->world_ctx, camera);
  EndMode3D();
  EndTextureMode();
  Profiler_EndSection();

  Profiler_BeginSection("PostProcess");
  BeginDrawing();
  ClearBackground(clear_color);
  if (!renderer->pixelate_enabled && renderer->fxaa_enabled && renderer->fxaa.id != 0) {
    BeginShaderMode(renderer->fxaa);
  }
  DrawTexturePro(renderer->target.texture,
                 (Rectangle){0.0f, 0.0f, (float)renderer->target.texture.width,
                             (float)-renderer->target.texture.height},
                 (Rectangle){0.0f, 0.0f, (float)GetScreenWidth(), (float)GetScreenHeight()},
                 (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
  if (!renderer->pixelate_enabled && renderer->fxaa_enabled && renderer->fxaa.id != 0) {
    EndShaderMode();
  }
  Profiler_EndSection();

  Profiler_BeginSection("HUD");
  if (callbacks->draw_hud != NULL) {
    callbacks->draw_hud(callbacks->hud_ctx);
  }
  Profiler_EndSection();

  Profiler_BeginSection("ImGui");
  if (callbacks->draw_ui != NULL) {
    callbacks->draw_ui(callbacks->ui_ctx);
  }
  Profiler_EndSection();

  EndDrawing();
}
