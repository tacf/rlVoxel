#ifndef VOXEL_CHUNKMAP_H
#define VOXEL_CHUNKMAP_H

#include "stb_ds.h"
#include "voxel/chunk.h"
#include <stdint.h>

typedef struct {
  int64_t key;
  VoxelChunk *value;
} VoxelChunkEntry;

typedef VoxelChunkEntry *VoxelChunkMap;

static inline VoxelChunkMap voxel_chunkmap_create(void) { return NULL; }

static inline void voxel_chunkmap_destroy(VoxelChunkMap *map) {
  if (map && *map) {
    stbds_hmfree(*map);
    *map = NULL;
  }
}

static inline int voxel_chunkmap_count(VoxelChunkMap map) { return map ? stbds_hmlen(map) : 0; }

static inline VoxelChunk *voxel_chunkmap_get(VoxelChunkMap map, int64_t key) {
  return map ? stbds_hmget(map, key) : NULL;
}

static inline void voxel_chunkmap_put(VoxelChunkMap *map, int64_t key, VoxelChunk *value) {
  stbds_hmput(*map, key, value);
}

static inline void voxel_chunkmap_remove(VoxelChunkMap *map, int64_t key) {
  if (map && *map) {
    stbds_hmdel(*map, key);
  }
}

static inline VoxelChunk *voxel_chunkmap_iter_value(VoxelChunkMap map, int idx) {
  return map[idx].value;
}

static inline int64_t voxel_chunkmap_iter_key(VoxelChunkMap map, int idx) { return map[idx].key; }

#endif
