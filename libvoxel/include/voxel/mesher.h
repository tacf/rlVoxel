#ifndef VOXEL_MESHER_H
#define VOXEL_MESHER_H

#include <stdint.h>
#include <stdbool.h>
#include "voxel/chunk.h"
#include "voxel/block.h"

/**
 * Raw mesh data output from the voxel mesher.
 * Contains vertex positions, texture coordinates, and colors.
 */
typedef struct VoxelMeshData {
  float *vertices;  /* 3 floats per vertex (x, y, z) */
  float *texcoords; /* 2 floats per vertex (u, v) */
  uint8_t *colors;  /* 4 bytes per vertex (r, g, b, a) */
  int vertex_count;
  int capacity;
} VoxelMeshData;

/**
 * Callbacks for the mesher to query world data.
 */
typedef struct VoxelMesherCallbacks {
  /* Get block at world coordinates */
  uint8_t (*get_world_block)(void *world_data, int wx, int y, int wz);

  /* Get skylight at world coordinates (0-15) */
  int (*get_world_skylight)(void *world_data, int wx, int y, int wz);

  /* User data pointer */
  void *world_data;
} VoxelMesherCallbacks;

/**
 * Mesher configuration.
 */
typedef struct VoxelMesherConfig {
  int chunk_size_x;
  int chunk_size_y;
  int chunk_size_z;
  float tile_size;     /* Size of one tile in texture atlas (e.g., 1/16 for 16x16 atlas) */
  float texel_padding; /* Padding in texels to avoid bleeding (e.g., 0.5) */
  float *light_lut;    /* Light level lookup table (16 entries, 0-15) */
} VoxelMesherConfig;

/* Initialize mesh data */
void VoxelMeshData_Init(VoxelMeshData *mesh);

/* Free mesh data */
void VoxelMeshData_Free(VoxelMeshData *mesh);

/* Clear mesh data without freeing */
void VoxelMeshData_Clear(VoxelMeshData *mesh);

/**
 * Generate mesh data for a chunk.
 * Outputs three separate meshes: solid, translucent, and cutout (plants).
 *
 * @param chunk The chunk to mesh
 * @param registry Block registry for querying block properties
 * @param config Mesher configuration
 * @param callbacks Callbacks for querying world data
 * @param solid Output mesh for solid blocks
 * @param translucent Output mesh for translucent blocks (water, ice)
 * @param cutout Output mesh for cutout blocks (grass, leaves)
 */
void VoxelMesher_BuildChunk(const VoxelChunk *chunk, const VoxelBlockRegistry *registry,
                            const VoxelMesherConfig *config, const VoxelMesherCallbacks *callbacks,
                            VoxelMeshData *solid, VoxelMeshData *translucent,
                            VoxelMeshData *cutout);

#endif
