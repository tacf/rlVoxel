#include "world/blocks.h"
#include "voxel/block.h"

#include <stdint.h>
#include <string.h>

#define TINT_RGB(R, G, B) {(R), (G), (B)}
#define TINT_ALL_RGB(R, G, B)                                                                      \
  {                                                                                                \
      TINT_RGB((R), (G), (B)), TINT_RGB((R), (G), (B)), TINT_RGB((R), (G), (B)),                   \
      TINT_RGB((R), (G), (B)), TINT_RGB((R), (G), (B)), TINT_RGB((R), (G), (B)),                   \
  }
#define TINT_ALL_WHITE TINT_ALL_RGB(255, 255, 255)

BlockDef g_block_defs[256];
VoxelBlockRegistry g_block_registry = {.blocks = g_block_defs, .count = 256};

void Blocks_Init(void) {
  memset(g_block_defs, 0, sizeof(g_block_defs));

  g_block_defs[BLOCK_STONE] = (BlockDef){
      .solid = true,
      .opaque = true,
      .translucent = false,
      .opacity = 15,
      .textures = {1, 1, 1, 1, 1, 1},
      .tint = TINT_ALL_WHITE,
  };

  g_block_defs[BLOCK_GRASS] = (BlockDef){
      .solid = true,
      .opaque = true,
      .translucent = false,
      .opacity = 15,
      .textures = {2, 0, 3, 3, 3, 3},
      .tint = {TINT_RGB(255, 255, 255), TINT_RGB(145, 189, 89), TINT_RGB(255, 255, 255),
               TINT_RGB(255, 255, 255), TINT_RGB(255, 255, 255), TINT_RGB(255, 255, 255)},
  };

  g_block_defs[BLOCK_DIRT] = (BlockDef){
      .solid = true,
      .opaque = true,
      .translucent = false,
      .opacity = 15,
      .textures = {2, 2, 2, 2, 2, 2},
      .tint = TINT_ALL_WHITE,
  };

  g_block_defs[BLOCK_BEDROCK] = (BlockDef){
      .solid = true,
      .opaque = true,
      .translucent = false,
      .opacity = 15,
      .textures = {17, 17, 17, 17, 17, 17},
      .tint = TINT_ALL_WHITE,
  };

  g_block_defs[BLOCK_WATER] = (BlockDef){
      .solid = false,
      .opaque = false,
      .translucent = true,
      .opacity = 3,
      .textures = {222, 222, 222, 222, 222, 222},
      .tint = TINT_ALL_WHITE,
  };

  g_block_defs[BLOCK_LAVA] = (BlockDef){
      .solid = false,
      .opaque = false,
      .translucent = true,
      .opacity = 3,
      .textures = {223, 223, 223, 223, 223, 223},
      .tint = TINT_ALL_RGB(255, 170, 96),
  };

  g_block_defs[BLOCK_SAND] = (BlockDef){
      .solid = true,
      .opaque = true,
      .translucent = false,
      .opacity = 15,
      .textures = {18, 18, 18, 18, 18, 18},
      .tint = TINT_ALL_WHITE,
  };

  g_block_defs[BLOCK_GRAVEL] = (BlockDef){
      .solid = true,
      .opaque = true,
      .translucent = false,
      .opacity = 15,
      .textures = {19, 19, 19, 19, 19, 19},
      .tint = TINT_ALL_WHITE,
  };

  g_block_defs[BLOCK_LOG] = (BlockDef){
      .solid = true,
      .opaque = true,
      .translucent = false,
      .opacity = 15,
      .textures = {21, 21, 20, 20, 20, 20},
      .tint = TINT_ALL_WHITE,
  };

  g_block_defs[BLOCK_LEAVES] = (BlockDef){
      .solid = true,
      .opaque = false,
      .translucent = true,
      .opacity = 1,
      .textures = {52, 52, 52, 52, 52, 52},
      .tint = TINT_ALL_RGB(128, 167, 85),
  };

  g_block_defs[BLOCK_SANDSTONE] = (BlockDef){
      .solid = true,
      .opaque = true,
      .translucent = false,
      .opacity = 15,
      .textures = {208, 176, 192, 192, 192, 192},
      .tint = TINT_ALL_WHITE,
  };

  g_block_defs[BLOCK_TALL_GRASS] = (BlockDef){
      .solid = false,
      .opaque = false,
      .translucent = true,
      .opacity = 0,
      .textures = {39, 39, 39, 39, 39, 39},
      .tint = TINT_ALL_RGB(146, 193, 98),
  };

  g_block_defs[BLOCK_ICE] = (BlockDef){
      .solid = true,
      .opaque = false,
      .translucent = true,
      .opacity = 3,
      .textures = {67, 67, 67, 67, 67, 67},
      .tint = TINT_ALL_WHITE,
  };
}

#undef TINT_ALL_WHITE
#undef TINT_ALL_RGB
#undef TINT_RGB
