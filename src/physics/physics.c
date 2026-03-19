#include "physics/physics.h"

#include <math.h>
#include <stdbool.h>

#include "constants.h"
#include "raylib.h"
#include "world/world.h"

/* Axis indices for cleaner code */
#define AXIS_X 0
#define AXIS_Y 1
#define AXIS_Z 2

/**
 * Creates an axis-aligned bounding box (AABB) at the given position.
 * The position represents the bottom-center of the player.
 */
BoundingBox Player_GetBoundsAtPosition(Vector3 position, Vector3 half_size, float height) {
  return (BoundingBox){
      .min = {position.x - half_size.x, position.y, position.z - half_size.z},
      .max = {position.x + half_size.x, position.y + height, position.z + half_size.z},
  };
}

/**
 * Checks collision using World_CheckAABB (which handles epsilon internally).
 */
static bool check_aabb_with_epsilon(const World *world, BoundingBox box) {
  return World_CheckAABB(world, box);
}

/**
 * Translates a bounding box along a single axis.
 */
static void translate_bounds_axis(BoundingBox *box, int axis, float delta) {
  if (axis == AXIS_X) {
    box->min.x += delta;
    box->max.x += delta;
  } else if (axis == AXIS_Y) {
    box->min.y += delta;
    box->max.y += delta;
  } else {
    box->min.z += delta;
    box->max.z += delta;
  }
}

/**
 * Translates a position along a single axis.
 */
static void translate_position_axis(Vector3 *position, int axis, float delta) {
  if (axis == AXIS_X) {
    position->x += delta;
  } else if (axis == AXIS_Y) {
    position->y += delta;
  } else {
    position->z += delta;
  }
}

/**
 * Binary search to find the exact collision point along an axis.
 * This is more precise than simple snapping and prevents corner sticking.
 */
static float resolve_step(const World *world, BoundingBox bounds, int axis, float step) {
  float low = 0.0f;
  float high = 1.0f;

  /* 8 iterations gives sufficient precision (2^-8 = 0.4%) */
  for (int i = 0; i < 8; i++) {
    float t = (low + high) * 0.5f;
    BoundingBox test = bounds;
    translate_bounds_axis(&test, axis, step * t);

    if (check_aabb_with_epsilon(world, test)) {
      high = t;
    } else {
      low = t;
    }
  }

  return step * low;
}

/**
 * Moves along a single axis with collision detection.
 * Uses sub-stepping for accuracy with high velocities.
 * Updates bounds as movement happens to prevent stale collision checks.
 */
static void move_axis(const World *world, Vector3 *position, float delta, int axis,
                      BoundingBox *bounds, bool *collided) {
  if (fabsf(delta) < 1e-6f) {
    return;
  }

  /* Sub-step to handle high velocities without tunneling */
  int steps = (int)ceilf(fabsf(delta) / PHYSICS_STEP_SIZE);
  if (steps < 1) {
    steps = 1;
  }

  float step = delta / (float)steps;

  for (int i = 0; i < steps; i++) {
    BoundingBox candidate = *bounds;
    translate_bounds_axis(&candidate, axis, step);

    if (check_aabb_with_epsilon(world, candidate)) {
      *collided = true;

      float actual_move = resolve_step(world, *bounds, axis, step);
      translate_position_axis(position, axis, actual_move);
      translate_bounds_axis(bounds, axis, actual_move);
      return;
    }

    translate_position_axis(position, axis, step);
    *bounds = candidate;
  }
}

/**
 * Main physics function: moves the player with collision response.
 * Handles X, Y, Z movement separately to allow sliding along walls.
 * Updates bounds between axes to prevent stale collision checks.
 */
void Physics_MoveWithCollision(const World *world, Vector3 *position, Vector3 velocity,
                               BoundingBox bounds, float dt, bool *on_ground,
                               Vector3 *result_velocity, PhysicsCollisionInfo *collision_info) {
  bool x_collided = false;
  bool y_collided = false;
  bool z_collided = false;

  /* Process each axis independently - allows sliding */
  move_axis(world, position, velocity.x * dt, AXIS_X, &bounds, &x_collided);
  move_axis(world, position, velocity.y * dt, AXIS_Y, &bounds, &y_collided);
  move_axis(world, position, velocity.z * dt, AXIS_Z, &bounds, &z_collided);

  /* Zero out velocity on collision axes */
  if (x_collided) {
    velocity.x = 0.0f;
  }
  if (z_collided) {
    velocity.z = 0.0f;
  }

  if (y_collided) {
    if (velocity.y < 0.0f) {
      *on_ground = true;
    }
    velocity.y = 0.0f;
  } else {
    *on_ground = false;
  }

  if (result_velocity) {
    *result_velocity = velocity;
  }

  if (collision_info) {
    collision_info->collided_x = x_collided;
    collision_info->collided_y = y_collided;
    collision_info->collided_z = z_collided;
  }
}
