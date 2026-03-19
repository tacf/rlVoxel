#include "game/raycast.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include <raymath.h>

#include "constants.h"
#include "raylib.h"
#include "world/blocks.h"
#include "world/chunk.h"
#include "world/world.h"

/**
 * Safe division that avoids division by zero.
 * Returns FLT_MAX when the input is near zero.
 */
static float safe_inv(float v) {
  if (fabsf(v) < 1e-6f) {
    return FLT_MAX;
  }
  return 1.0f / v;
}

/**
 * Performs a voxel raycast using the Amanatides & Woo algorithm.
 * This is a fast voxel traversal algorithm that walks through the grid
 * one voxel at a time, always stepping to the nearest boundary.
 */
static bool Raycast_Voxel_Internal(const World *world, Vector3 origin, Vector3 direction,
                                   float max_distance, bool include_replaceable,
                                   VoxelRaycastHit *out_hit) {
  memset(out_hit, 0, sizeof(*out_hit));

  if (Vector3Length(direction) < 1e-6f) {
    return false;
  }

  Vector3 dir = Vector3Normalize(direction);

  /* Start in the voxel containing the origin */
  int x = (int)floorf(origin.x);
  int y = (int)floorf(origin.y);
  int z = (int)floorf(origin.z);

  /* Determine step direction for each axis (-1, 0, or 1) */
  int step_x = (dir.x > 0.0f) ? 1 : (dir.x < 0.0f ? -1 : 0);
  int step_y = (dir.y > 0.0f) ? 1 : (dir.y < 0.0f ? -1 : 0);
  int step_z = (dir.z > 0.0f) ? 1 : (dir.z < 0.0f ? -1 : 0);

  /* Precompute inverse direction (1/dir) for fast boundary calculations */
  float inv_x = safe_inv(dir.x);
  float inv_y = safe_inv(dir.y);
  float inv_z = safe_inv(dir.z);

  /* Distance between voxel boundaries (constant for each axis) */
  float t_delta_x = fabsf(inv_x);
  float t_delta_y = fabsf(inv_y);
  float t_delta_z = fabsf(inv_z);

  /* Distance to first voxel boundary in each direction */
  float next_boundary_x = (step_x > 0) ? ((float)x + 1.0f - origin.x) : (origin.x - (float)x);
  float next_boundary_y = (step_y > 0) ? ((float)y + 1.0f - origin.y) : (origin.y - (float)y);
  float next_boundary_z = (step_z > 0) ? ((float)z + 1.0f - origin.z) : (origin.z - (float)z);

  /* t_max = distance to first boundary crossed */
  float t_max_x = (step_x == 0) ? FLT_MAX : next_boundary_x * t_delta_x;
  float t_max_y = (step_y == 0) ? FLT_MAX : next_boundary_y * t_delta_y;
  float t_max_z = (step_z == 0) ? FLT_MAX : next_boundary_z * t_delta_z;

  /* Normal points in the opposite direction of stepping */
  int normal_x = 0;
  int normal_y = 0;
  int normal_z = 0;

  float traveled = 0.0f;

  /* Walk through voxels until we hit something or exceed max distance */
  while (traveled <= max_distance && y >= -1 && y < WORLD_MAX_HEIGHT + 1) {
    uint8_t block_id = World_GetBlock(world, x, y, z);

    bool is_target = !Block_IsLiquid(block_id) && Block_IsSolid(block_id);
    if (include_replaceable && block_id != BLOCK_AIR && !Block_IsLiquid(block_id) &&
        Block_IsReplaceable(block_id)) {
      is_target = true;
    }

    if (is_target) {
      out_hit->hit = true;
      out_hit->block_x = x;
      out_hit->block_y = y;
      out_hit->block_z = z;
      out_hit->normal_x = normal_x;
      out_hit->normal_y = normal_y;
      out_hit->normal_z = normal_z;
      out_hit->block_id = block_id;
      out_hit->distance = traveled;
      return true;
    }

    /* Step to the nearest voxel boundary - this is the core of the algorithm */
    if (t_max_x < t_max_y) {
      if (t_max_x < t_max_z) {
        x += step_x;
        traveled = t_max_x;
        t_max_x += t_delta_x;
        normal_x = -step_x;
        normal_y = 0;
        normal_z = 0;
      } else {
        z += step_z;
        traveled = t_max_z;
        t_max_z += t_delta_z;
        normal_x = 0;
        normal_y = 0;
        normal_z = -step_z;
      }
    } else {
      if (t_max_y < t_max_z) {
        y += step_y;
        traveled = t_max_y;
        t_max_y += t_delta_y;
        normal_x = 0;
        normal_y = -step_y;
        normal_z = 0;
      } else {
        z += step_z;
        traveled = t_max_z;
        t_max_z += t_delta_z;
        normal_x = 0;
        normal_y = 0;
        normal_z = -step_z;
      }
    }
  }

  return false;
}

bool Raycast_Voxel(const World *world, Vector3 origin, Vector3 direction, float max_distance,
                   VoxelRaycastHit *out_hit) {
  return Raycast_Voxel_Internal(world, origin, direction, max_distance, false, out_hit);
}

bool Raycast_VoxelForPlacement(const World *world, Vector3 origin, Vector3 direction,
                               float max_distance, VoxelRaycastHit *out_hit) {
  return Raycast_Voxel_Internal(world, origin, direction, max_distance, true, out_hit);
}
