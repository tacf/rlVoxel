#ifndef VOXEL_CHUNK_H
#define VOXEL_CHUNK_H

#include <stdbool.h>
#include <stdint.h>

#define VOXEL_CHUNK_SIZE_X 16
#define VOXEL_CHUNK_SIZE_Y 128
#define VOXEL_CHUNK_SIZE_Z 16
#define VOXEL_CHUNK_HEIGHTMAP (VOXEL_CHUNK_SIZE_X * VOXEL_CHUNK_SIZE_Z)

// Flags bitwise operation macros
#define FLAG_SET(n, f) ((n) |= (f))
#define FLAG_CLEAR(n, f) ((n) &= ~(f))
#define FLAG_TOGGLE(n, f) ((n) ^= (f))
#define FLAG_IS_SET(n, f) (((n) & (f)) == (f))

enum VoxelChunkState { GENERATED = 1u << 0, DIRTY = 1u << 1, LIGHTDIRTY = 1u << 2 };
enum VoxelChunkRenderPass {
  SOLID = 1u << 0,
  TRANSLUCENT = 1u << 1,
  CUTOUT = 1u << 2,
  TRANSLUCENT_SOLID = 1u << 3
};

typedef struct VoxelChunk {
  int cx;
  int cz;

  uint8_t *blocks;
  uint8_t *skylight;
  uint8_t heightmap[VOXEL_CHUNK_HEIGHTMAP];

  uint8_t state;
  uint8_t passes;

  void *solid_model;
  void *translucent_model;
  void *cutout_model;
  void *translucent_solid_model;
} VoxelChunk;

void VoxelChunk_Init(VoxelChunk *chunk, int cx, int cz);
void VoxelChunk_Shutdown(VoxelChunk *chunk, void (*unload_model)(void *));

int VoxelChunk_Index(int lx, int y, int lz);

void VoxelChunk_SetState(VoxelChunk *chunk, uint8_t state);
void VoxelChunk_UnsetState(VoxelChunk *chunk, uint8_t state);
bool VoxelChunk_HasState(const VoxelChunk *chunk, uint8_t state);

void VoxelChunk_EnableRenderPass(VoxelChunk *chunk, uint8_t pass);
void VoxelChunk_DisableRenderPass(VoxelChunk *chunk, uint8_t pass);
bool VoxelChunk_HasRenderPass(const VoxelChunk *chunk, uint8_t pass);

uint8_t VoxelChunk_GetBlock(const VoxelChunk *chunk, int lx, int y, int lz);
void VoxelChunk_SetBlock(VoxelChunk *chunk, int lx, int y, int lz, uint8_t block_id);

int VoxelChunk_GetSkyLight(const VoxelChunk *chunk, int lx, int y, int lz);
void VoxelChunk_SetSkyLight(VoxelChunk *chunk, int lx, int y, int lz, int value);

int VoxelChunk_GetHeight(const VoxelChunk *chunk, int lx, int lz);
void VoxelChunk_RecomputeHeightAll(VoxelChunk *chunk, int (*block_opacity)(uint8_t));
void VoxelChunk_RecomputeHeightColumn(VoxelChunk *chunk, int lx, int lz,
                                      int (*block_opacity)(uint8_t));

#endif
