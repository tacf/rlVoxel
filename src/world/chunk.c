#include "world/chunk.h"
#include "raylib.h"
#include "voxel/chunk.h"
#include "world/blocks.h"

#include <stdlib.h>

void Chunk_Shutdown(Chunk *chunk) {
  /* Unload raylib models */
  if (VoxelChunk_HasRenderPass(chunk, SOLID) && chunk->solid_model) {
    UnloadModel(*(Model *)chunk->solid_model);
    free(chunk->solid_model);
    chunk->solid_model = NULL;
  }
  if (VoxelChunk_HasRenderPass(chunk, TRANSLUCENT) && chunk->translucent_model) {
    UnloadModel(*(Model *)chunk->translucent_model);
    free(chunk->translucent_model);
    chunk->translucent_model = NULL;
  }
  if (VoxelChunk_HasRenderPass(chunk, CUTOUT) && chunk->cutout_model) {
    UnloadModel(*(Model *)chunk->cutout_model);
    free(chunk->cutout_model);
    chunk->cutout_model = NULL;
  }
  if (VoxelChunk_HasRenderPass(chunk, TRANSLUCENT_SOLID) && chunk->translucent_solid_model) {
    UnloadModel(*(Model *)chunk->translucent_solid_model);
    free(chunk->translucent_solid_model);
    chunk->translucent_solid_model = NULL;
  }
  VoxelChunk_Shutdown(chunk, NULL);
}

void Chunk_RecomputeHeightColumn(Chunk *chunk, int lx, int lz) {
  VoxelChunk_RecomputeHeightColumn(chunk, lx, lz, Block_Opacity);
}

void Chunk_RecomputeHeightAll(Chunk *chunk) { VoxelChunk_RecomputeHeightAll(chunk, Block_Opacity); }
