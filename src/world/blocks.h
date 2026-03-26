#ifndef RLVOXEL_BLOCKS_H
#define RLVOXEL_BLOCKS_H

#include <stdint.h>
#include <voxel/block.h>

/**
 * Face directions (game aliases for voxel lib constants)
 */
#define FACE_DOWN VOXEL_FACE_DOWN
#define FACE_UP VOXEL_FACE_UP
#define FACE_NORTH VOXEL_FACE_NORTH
#define FACE_SOUTH VOXEL_FACE_SOUTH
#define FACE_WEST VOXEL_FACE_WEST
#define FACE_EAST VOXEL_FACE_EAST

/**
 * Block IDs for rl-voxel game. Smililarities with other games 
 * are mere coincidences x)
 */
typedef enum BlockId {
  BLOCK_AIR = 0,
  BLOCK_STONE = 1,
  BLOCK_GRASS = 2,
  BLOCK_DIRT = 3,
  BLOCK_BEDROCK = 7,
  BLOCK_WATER = 9,
  BLOCK_LAVA = 10,
  BLOCK_SAND = 12,
  BLOCK_GRAVEL = 13,
  BLOCK_LOG = 17,
  BLOCK_LEAVES = 18,
  BLOCK_SANDSTONE = 24,
  BLOCK_TALL_GRASS = 31,
  BLOCK_ICE = 79
} BlockId;

/* Game-specific block definition type */
typedef VoxelBlockDef BlockDef;

/* Global block registry */
extern VoxelBlockRegistry g_block_registry;
extern BlockDef g_block_defs[256];

/**
 * Initializes the block definitions for rl-voxel.
 * Must be called once at startup.
 */
void Blocks_Init(void);
int Block_GetDurability(uint8_t block_id);

/* Wrapper functions for game code */
static inline bool Block_IsSolid(uint8_t block_id) {
  return VoxelBlock_IsSolid(&g_block_registry, block_id);
}

static inline bool Block_IsOpaque(uint8_t block_id) {
  return VoxelBlock_IsOpaque(&g_block_registry, block_id);
}

static inline bool Block_IsTranslucent(uint8_t block_id) {
  return VoxelBlock_IsTranslucent(&g_block_registry, block_id);
}

static inline bool Block_IsLiquid(uint8_t block_id) {
  return (block_id == BLOCK_WATER) || (block_id == BLOCK_LAVA);
}

static inline bool Block_IsReplaceable(uint8_t block_id) {
  switch (block_id) {
  case BLOCK_AIR:
  case BLOCK_WATER:
  case BLOCK_LAVA:
  case BLOCK_TALL_GRASS:
    return true;
  default:
    return false;
  }
}

static inline int Block_Opacity(uint8_t block_id) {
  return VoxelBlock_Opacity(&g_block_registry, block_id);
}

static inline int Block_Texture(uint8_t block_id, int face) {
  return VoxelBlock_Texture(&g_block_registry, block_id, face);
}

static inline void Block_GetFaceTint(uint8_t block_id, int face, uint8_t *r, uint8_t *g,
                                     uint8_t *b) {
  VoxelBlock_GetTint(&g_block_registry, block_id, face, r, g, b);
}

#endif /* RLVOXEL_BLOCKS_H */
