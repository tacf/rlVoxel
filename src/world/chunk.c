#include "world/chunk.h"
#include "raylib.h"
#include "voxel/chunk.h"
#include "world/blocks.h"

#include <stdlib.h>

void **Chunk_ModelSlot(Chunk *chunk, uint8_t pass) {
  switch (pass) {
  case SOLID:
    return &chunk->solid_model;
  case TRANSLUCENT:
    return &chunk->translucent_model;
  case CUTOUT:
    return &chunk->cutout_model;
  default:
    return &chunk->translucent_solid_model;
  }
}

static void chunk_unload_model(Chunk *chunk, uint8_t pass) {
  void **slot = Chunk_ModelSlot(chunk, pass);
  if (VoxelChunk_HasRenderPass(chunk, pass) && *slot) {
    UnloadModel(*(Model *)*slot);
    free(*slot);
    *slot = NULL;
  }
}

void Chunk_Shutdown(Chunk *chunk) {
  chunk_unload_model(chunk, SOLID);
  chunk_unload_model(chunk, TRANSLUCENT);
  chunk_unload_model(chunk, CUTOUT);
  chunk_unload_model(chunk, TRANSLUCENT_SOLID);
  VoxelChunk_Shutdown(chunk, NULL);
}

void Chunk_RecomputeHeightColumn(Chunk *chunk, int lx, int lz) {
  VoxelChunk_RecomputeHeightColumn(chunk, lx, lz, Block_Opacity);
}

void Chunk_RecomputeHeightAll(Chunk *chunk) { VoxelChunk_RecomputeHeightAll(chunk, Block_Opacity); }
