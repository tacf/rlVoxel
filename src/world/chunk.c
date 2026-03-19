#include "world/chunk.h"
#include "world/blocks.h"

#include <stdlib.h>

void Chunk_Shutdown(Chunk *chunk) {
  /* Unload raylib models */
  if (chunk->has_solid_model && chunk->solid_model) {
    UnloadModel(*(Model *)chunk->solid_model);
    free(chunk->solid_model);
    chunk->solid_model = NULL;
  }
  if (chunk->has_translucent_model && chunk->translucent_model) {
    UnloadModel(*(Model *)chunk->translucent_model);
    free(chunk->translucent_model);
    chunk->translucent_model = NULL;
  }
  if (chunk->has_cutout_model && chunk->cutout_model) {
    UnloadModel(*(Model *)chunk->cutout_model);
    free(chunk->cutout_model);
    chunk->cutout_model = NULL;
  }
  VoxelChunk_Shutdown(chunk, NULL);
}

void Chunk_RecomputeHeightColumn(Chunk *chunk, int lx, int lz) {
  VoxelChunk_RecomputeHeightColumn(chunk, lx, lz, Block_Opacity);
}

void Chunk_RecomputeHeightAll(Chunk *chunk) { VoxelChunk_RecomputeHeightAll(chunk, Block_Opacity); }
