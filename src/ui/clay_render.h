#ifndef RLVOXEL_UI_CLAY_RENDER_H
#define RLVOXEL_UI_CLAY_RENDER_H

#include <raylib.h>

#include <clay.h>

/* Turns a Clay_RenderCommandArray into raylib draw calls. Stateless: Clay's
 * layout pass is immediate mode by design, so callers redeclare the whole
 * tree every frame and this module just walks the result. Point the
 * userData in Clay_SetMeasureTextFunction and the font passed to
 * ClayRender_Draw at the same Font.
 *
 * Caching is the caller's job. If some subtree barely changes frame to
 * frame, bake it into a RenderTexture2D and skip Clay for it entirely -
 * see how src/ui/hud.c handles the hotbar background. */

Clay_Dimensions ClayRender_MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config,
                                       void *userData);

void ClayRender_Draw(Clay_RenderCommandArray commands, Font *font);

#endif /* RLVOXEL_UI_CLAY_RENDER_H */
