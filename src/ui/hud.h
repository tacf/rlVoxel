#ifndef RLVOXEL_UI_HUD_H
#define RLVOXEL_UI_HUD_H

#include "game/player.h"
#include "ui/ui.h"

#include <raylib.h>

void HUD_BuildInfoPanel(UiContext *ui, const Player *player, const World *world);
void HUD_BuildHotbar(UiContext *ui, Texture2D terrain_texture, const Player *player);

#endif /* RLVOXEL_UI_HUD_H */
