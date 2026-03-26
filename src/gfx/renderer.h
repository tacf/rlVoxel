#ifndef RLVOXEL_RENDERER_H
#define RLVOXEL_RENDERER_H

#include <stdbool.h>

#include <raylib.h>

typedef void (*RendererWorldPassFn)(void *ctx, const Camera3D *camera);
typedef void (*RendererOverlayPassFn)(void *ctx);

typedef struct RendererFrameCallbacks {
  RendererWorldPassFn draw_world;
  void *world_ctx;
  RendererOverlayPassFn draw_hud;
  void *hud_ctx;
  RendererOverlayPassFn draw_ui;
  void *ui_ctx;
} RendererFrameCallbacks;

typedef struct Renderer {
  Shader fxaa;
  int fxaa_resolution_loc;
  RenderTexture2D target;
  bool pixelate_enabled;
  bool fxaa_enabled;
  int pixelate_downscale;
} Renderer;

bool Renderer_Init(Renderer *renderer);
void Renderer_Shutdown(Renderer *renderer);
void Renderer_SetPixelateEnabled(Renderer *renderer, bool enabled);
bool Renderer_GetPixelateEnabled(const Renderer *renderer);
void Renderer_DrawFrame(Renderer *renderer, const Camera3D *camera, Color clear_color,
                        const RendererFrameCallbacks *callbacks);

#endif /* RLVOXEL_RENDERER_H */
