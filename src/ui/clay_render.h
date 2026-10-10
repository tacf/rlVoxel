#ifndef RLVOXEL_UI_CLAY_RENDER_H
#define RLVOXEL_UI_CLAY_RENDER_H

#include <raylib.h>

#include <clay.h>

/* One application-owned context shared by menus and the gameplay HUD. */
bool ClayRender_Init(Font *font);
void ClayRender_Shutdown(void);
void ClayRender_BeginFrame(Font *font, bool pointer_enabled);

/* Turns a Clay_RenderCommandArray into raylib draw calls, preserving nested
 * clipping regions. Clay's layout is declared again each frame; BeginFrame
 * selects the font used by both measurement and rendering.
 *
 * Caching is the caller's job. If some subtree barely changes frame to
 * frame, bake it into a RenderTexture2D and skip Clay for it entirely -
 * see how src/ui/hud.c handles the hotbar background. */

Clay_Dimensions ClayRender_MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config,
                                       void *userData);

void ClayRender_Draw(Clay_RenderCommandArray commands, Font *font);

#endif /* RLVOXEL_UI_CLAY_RENDER_H */
