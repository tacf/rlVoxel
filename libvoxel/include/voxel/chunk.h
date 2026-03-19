#ifndef VOXEL_CHUNK_H
#define VOXEL_CHUNK_H

#include <stdbool.h>
#include <stdint.h>

#define VOXEL_CHUNK_SIZE_X 16
#define VOXEL_CHUNK_SIZE_Y 128
#define VOXEL_CHUNK_SIZE_Z 16
#define VOXEL_CHUNK_HEIGHTMAP (VOXEL_CHUNK_SIZE_X * VOXEL_CHUNK_SIZE_Z)

typedef struct VoxelChunk {
  int cx;
  int cz;

  uint8_t *blocks;
  uint8_t *skylight;
  uint8_t heightmap[VOXEL_CHUNK_HEIGHTMAP];

  bool generated;
  bool mesh_dirty;
  bool lighting_dirty;

  bool has_solid_model;
  bool has_translucent_model;
  bool has_cutout_model;
  void *solid_model;
  void *translucent_model;
  void *cutout_model;
} VoxelChunk;

void VoxelChunk_Init(VoxelChunk *chunk, int cx, int cz);
void VoxelChunk_Shutdown(VoxelChunk *chunk, void (*unload_model)(void *));

int VoxelChunk_Index(int lx, int y, int lz);

uint8_t VoxelChunk_GetBlock(const VoxelChunk *chunk, int lx, int y, int lz);
void VoxelChunk_SetBlock(VoxelChunk *chunk, int lx, int y, int lz, uint8_t block_id);

int VoxelChunk_GetSkyLight(const VoxelChunk *chunk, int lx, int y, int lz);
void VoxelChunk_SetSkyLight(VoxelChunk *chunk, int lx, int y, int lz, int value);

int VoxelChunk_GetHeight(const VoxelChunk *chunk, int lx, int lz);
void VoxelChunk_RecomputeHeightAll(VoxelChunk *chunk, int (*block_opacity)(uint8_t));
void VoxelChunk_RecomputeHeightColumn(VoxelChunk *chunk, int lx, int lz,
                                      int (*block_opacity)(uint8_t));

#endif
