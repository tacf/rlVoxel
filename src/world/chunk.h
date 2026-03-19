#ifndef RLVOXEL_CHUNK_H
#define RLVOXEL_CHUNK_H

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

static inline int Chunk_Index(int lx, int y, int lz) { return VoxelChunk_Index(lx, y, lz); }

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
