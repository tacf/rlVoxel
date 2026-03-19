#include "voxel/block.h"

bool VoxelBlock_IsSolid(const VoxelBlockRegistry *registry, uint8_t block_id) {
  if (!registry || block_id >= registry->count) {
    return false;
  }
  return registry->blocks[block_id].solid;
}

bool VoxelBlock_IsOpaque(const VoxelBlockRegistry *registry, uint8_t block_id) {
  if (!registry || block_id >= registry->count) {
    return false;
  }
  return registry->blocks[block_id].opaque;
}

bool VoxelBlock_IsTranslucent(const VoxelBlockRegistry *registry, uint8_t block_id) {
  if (!registry || block_id >= registry->count) {
    return false;
  }
  return registry->blocks[block_id].translucent;
}

int VoxelBlock_Opacity(const VoxelBlockRegistry *registry, uint8_t block_id) {
  if (!registry || block_id >= registry->count) {
    return 0;
  }
  int opacity = registry->blocks[block_id].opacity;
  if (opacity < 0) {
    return 0;
  }
  if (opacity > 15) {
    return 15;
  }
  return opacity;
}

int VoxelBlock_Texture(const VoxelBlockRegistry *registry, uint8_t block_id, int face) {
  if (!registry || block_id >= registry->count || face < 0 || face > 5) {
    return 0;
  }
  return registry->blocks[block_id].textures[face];
}

void VoxelBlock_GetTint(const VoxelBlockRegistry *registry, uint8_t block_id, int face, uint8_t *r,
                        uint8_t *g, uint8_t *b) {
  if (!registry || block_id >= registry->count || face < 0 || face > 5) {
    if (r)
      *r = 255;
    if (g)
      *g = 255;
    if (b)
      *b = 255;
    return;
  }

  if (r)
    *r = registry->blocks[block_id].tint[face][0];
  if (g)
    *g = registry->blocks[block_id].tint[face][1];
  if (b)
    *b = registry->blocks[block_id].tint[face][2];
}
