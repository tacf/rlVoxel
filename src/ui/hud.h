#ifndef RLVOXEL_UI_HUD_H
#define RLVOXEL_UI_HUD_H

#include "game/player.h"
#include "world/world.h"

#include <raylib.h>

/* Declares this frame's Clay elements; call between Clay_BeginLayout() and
 * Clay_EndLayout(). */
void HUD_BuildInfoPanel(const Player *player, const World *world);
void HUD_BuildHotbar(const Player *player);

/* Draws the hotbar's background (baked into a retained RenderTexture2D, only
 * regenerated when its inputs actually change) and per-slot icons. Call after
 * Clay_EndLayout() and before ClayRender_Draw() so pause overlays cover it,
 * once Clay_GetElementData() can resolve "hud_hotbar_panel"'s final
 * position for this frame. */
void HUD_DrawHotbar(Texture2D terrain_texture, const Player *player);
void HUD_Shutdown(void);

#endif /* RLVOXEL_UI_HUD_H */
