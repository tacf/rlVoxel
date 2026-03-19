#ifndef VOXEL_BLOCK_H
#define VOXEL_BLOCK_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Face directions for block rendering and culling.
 */
enum {
  VOXEL_FACE_DOWN = 0,
  VOXEL_FACE_UP = 1,
  VOXEL_FACE_NORTH = 2,
  VOXEL_FACE_SOUTH = 3,
  VOXEL_FACE_WEST = 4,
  VOXEL_FACE_EAST = 5
};

/**
 * Block definition containing rendering and physics properties.
 */
typedef struct VoxelBlockDef {
  bool solid;
  bool opaque;
  bool translucent;
  int opacity;
  int textures[6];
  uint8_t tint[6][3]; /* RGB tint for each face (255,255,255 = no tint) */
} VoxelBlockDef;

/**
 * Block registry interface.
 * Games should provide their own block definitions array.
 */
typedef struct VoxelBlockRegistry {
  VoxelBlockDef *blocks;
  int count;
} VoxelBlockRegistry;

/* Block query functions - operate on a registry */
bool VoxelBlock_IsSolid(const VoxelBlockRegistry *registry, uint8_t block_id);
bool VoxelBlock_IsOpaque(const VoxelBlockRegistry *registry, uint8_t block_id);
bool VoxelBlock_IsTranslucent(const VoxelBlockRegistry *registry, uint8_t block_id);
int VoxelBlock_Opacity(const VoxelBlockRegistry *registry, uint8_t block_id);
int VoxelBlock_Texture(const VoxelBlockRegistry *registry, uint8_t block_id, int face);
void VoxelBlock_GetTint(const VoxelBlockRegistry *registry, uint8_t block_id, int face, uint8_t *r,
                        uint8_t *g, uint8_t *b);

#endif
