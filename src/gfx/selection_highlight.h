#ifndef RLVOXEL_SELECTION_HIGHLIGHT_H
#define RLVOXEL_SELECTION_HIGHLIGHT_H

#include "game/raycast.h"
#include "raylib.h"

void SelectionHighlight_Draw(const VoxelRaycastHit *hit);
void SelectionHighlight_DrawDamageOverlay(Texture2D terrain_texture, int block_x, int block_y,
                                          int block_z, int crack_stage);

#endif /* RLVOXEL_SELECTION_HIGHLIGHT_H */
