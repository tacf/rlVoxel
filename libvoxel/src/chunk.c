#include "voxel/chunk.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VOXEL_CHUNK_VOLUME (VOXEL_CHUNK_SIZE_X * VOXEL_CHUNK_SIZE_Y * VOXEL_CHUNK_SIZE_Z)
#define VOXEL_CHUNK_LIGHT_BYTES (VOXEL_CHUNK_VOLUME / 2)

static int clamp_light(int light) {
  if (light < 0) {
    return 0;
  }
  if (light > 15) {
    return 15;
  }
  return light;
}

void VoxelChunk_Init(VoxelChunk *chunk, int cx, int cz) {
  memset(chunk, 0, sizeof(*chunk));
  chunk->cx = cx;
  chunk->cz = cz;
  chunk->blocks = (uint8_t *)calloc(VOXEL_CHUNK_VOLUME, sizeof(uint8_t));
  chunk->skylight = (uint8_t *)calloc(VOXEL_CHUNK_LIGHT_BYTES, sizeof(uint8_t));
  chunk->mesh_dirty = true;
  chunk->lighting_dirty = true;
}

void VoxelChunk_Shutdown(VoxelChunk *chunk, void (*unload_model)(void *)) {
  if (unload_model) {
    if (chunk->has_solid_model && chunk->solid_model) {
      unload_model(chunk->solid_model);
    }
    if (chunk->has_translucent_model && chunk->translucent_model) {
      unload_model(chunk->translucent_model);
    }
    if (chunk->has_cutout_model && chunk->cutout_model) {
      unload_model(chunk->cutout_model);
    }
  }
  free(chunk->blocks);
  free(chunk->skylight);
  chunk->blocks = NULL;
  chunk->skylight = NULL;
}

int VoxelChunk_Index(int lx, int y, int lz) { return (lx << 11) | (lz << 7) | y; }

uint8_t VoxelChunk_GetBlock(const VoxelChunk *chunk, int lx, int y, int lz) {
  return chunk->blocks[VoxelChunk_Index(lx, y, lz)];
}

void VoxelChunk_SetBlock(VoxelChunk *chunk, int lx, int y, int lz, uint8_t block_id) {
  chunk->blocks[VoxelChunk_Index(lx, y, lz)] = block_id;
  chunk->mesh_dirty = true;
  chunk->lighting_dirty = true;
}

int VoxelChunk_GetSkyLight(const VoxelChunk *chunk, int lx, int y, int lz) {
  int idx = VoxelChunk_Index(lx, y, lz);
  int byte_index = idx >> 1;
  uint8_t packed = chunk->skylight[byte_index];
  if ((idx & 1) == 0) {
    return packed & 0x0F;
  }
  return (packed >> 4) & 0x0F;
}

void VoxelChunk_SetSkyLight(VoxelChunk *chunk, int lx, int y, int lz, int value) {
  int idx = VoxelChunk_Index(lx, y, lz);
  int byte_index = idx >> 1;
  int light = clamp_light(value);
  uint8_t packed = chunk->skylight[byte_index];
  if ((idx & 1) == 0) {
    packed = (uint8_t)((packed & 0xF0) | (light & 0x0F));
  } else {
    packed = (uint8_t)((packed & 0x0F) | ((light & 0x0F) << 4));
  }
  chunk->skylight[byte_index] = packed;
}

int VoxelChunk_GetHeight(const VoxelChunk *chunk, int lx, int lz) {
  return chunk->heightmap[(lz << 4) | lx];
}

void VoxelChunk_RecomputeHeightColumn(VoxelChunk *chunk, int lx, int lz,
                                      int (*block_opacity)(uint8_t)) {
  int y = VOXEL_CHUNK_SIZE_Y - 1;
  int idx_base = (lx << 11) | (lz << 7);

  while (y > 0) {
    uint8_t block_id = chunk->blocks[idx_base + y - 1];
    int opacity = block_opacity ? block_opacity(block_id) : 15;
    if (opacity > 0) {
      break;
    }
    y--;
  }

  chunk->heightmap[(lz << 4) | lx] = (uint8_t)y;
}

void VoxelChunk_RecomputeHeightAll(VoxelChunk *chunk, int (*block_opacity)(uint8_t)) {
  for (int lx = 0; lx < VOXEL_CHUNK_SIZE_X; lx++) {
    for (int lz = 0; lz < VOXEL_CHUNK_SIZE_Z; lz++) {
      VoxelChunk_RecomputeHeightColumn(chunk, lx, lz, block_opacity);
    }
  }
}
