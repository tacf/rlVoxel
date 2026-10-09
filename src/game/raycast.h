#ifndef RLVOXEL_RAYCAST_H
#define RLVOXEL_RAYCAST_H

#include <stdbool.h>

#include <raylib.h>
#include <stdint.h>

#include "world/world.h"

/**
 * Result of a voxel raycast hit.
 * Contains information about which block was hit and from which direction.
 */
typedef struct VoxelRaycastHit {
  bool hit;         /**< True if a block was hit */
  int block_x;      /**< X coordinate of the hit block */
  int block_y;      /**< Y coordinate of the hit block */
  int block_z;      /**< Z coordinate of the hit block */
  int normal_x;     /**< X component of hit normal (-1, 0, or 1) */
  int normal_y;     /**< Y component of hit normal (-1, 0, or 1) */
  int normal_z;     /**< Z component of hit normal (-1, 0, or 1) */
  uint8_t block_id; /**< Block type that was hit */
  float distance;   /**< Distance from origin to hit point */
} VoxelRaycastHit;

/**
 * Voxel raycast using the Amanatides & Woo algorithm.
 * Returns the first solid or replaceable non-liquid block hit (for example
 * tall grass).
 *
 * @param direction     Direction to cast the ray (will be normalized)
 * @param max_distance  Maximum distance to cast
 */
bool Raycast_VoxelForPlacement(const World *world, Vector3 origin, Vector3 direction,
                               float max_distance, VoxelRaycastHit *out_hit);

#endif /* RLVOXEL_RAYCAST_H */
