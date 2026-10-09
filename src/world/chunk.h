#ifndef RLVOXEL_CHUNK_H
#define RLVOXEL_CHUNK_H

#include <stdint.h>
#include <voxel/chunk.h>
#include <raylib.h>

/**
 * Game-specific chunk type that wraps VoxelChunk.
 * Uses VoxelChunk from libvoxel with raylib Model types.
 */
typedef VoxelChunk Chunk;

/* Inline wrapper functions for game code */
static inline void Chunk_Init(Chunk *chunk, int cx, int cz) { VoxelChunk_Init(chunk, cx, cz); }

void Chunk_Shutdown(Chunk *chunk);

/* Pointer to the Model* a chunk keeps for the given VoxelChunkRenderPass. */
void **Chunk_ModelSlot(Chunk *chunk, uint8_t pass);

static inline int64_t make_chunk_key(int cx, int cz) {
  /* Multiplication preserves signed chunk coordinates without shifting a negative value.
   * Avoids undefined this way.
   */
  return (int64_t)cx * INT64_C(4294967296) + (uint32_t)cz;
}

static inline int floor_div16(int x) {
  /* C division truncates toward zero, shenanigans with negative remainders. */
  return x / 16 - (x % 16 < 0);
}

static inline int floor_mod16(int x) { return (unsigned int)x & 15u; }

static inline uint8_t Chunk_GetBlock(const Chunk *chunk, int lx, int y, int lz) {
  return VoxelChunk_GetBlock(chunk, lx, y, lz);
}

static inline void Chunk_SetBlock(Chunk *chunk, int lx, int y, int lz, uint8_t block_id) {
  VoxelChunk_SetBlock(chunk, lx, y, lz, block_id);
}

static inline int Chunk_GetSkyLight(const Chunk *chunk, int lx, int y, int lz) {
  return VoxelChunk_GetSkyLight(chunk, lx, y, lz);
}

static inline void Chunk_SetSkyLight(Chunk *chunk, int lx, int y, int lz, int value) {
  VoxelChunk_SetSkyLight(chunk, lx, y, lz, value);
}

static inline int Chunk_GetHeight(const Chunk *chunk, int lx, int lz) {
  return VoxelChunk_GetHeight(chunk, lx, lz);
}

void Chunk_RecomputeHeightAll(Chunk *chunk);
void Chunk_RecomputeHeightColumn(Chunk *chunk, int lx, int lz);

#endif /* RLVOXEL_CHUNK_H */
