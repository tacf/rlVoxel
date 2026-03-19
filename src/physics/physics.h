#ifndef RLVOXEL_PHYSICS_H
#define RLVOXEL_PHYSICS_H

#include <stdbool.h>

#include <raylib.h>

/* Forward declaration */
typedef struct World World;

typedef struct PhysicsCollisionInfo {
  bool collided_x;
  bool collided_y;
  bool collided_z;
} PhysicsCollisionInfo;

/**
 * Moves a position with collision detection and response.
 *
 * @param world         The world to check collisions against
 * @param position      Current position (modified in place)
 * @param velocity      Velocity vector (blocks per second)
 * @param bounds        Axis-aligned bounding box of the player
 * @param dt            Delta time in seconds
 * @param on_ground     Output: true if standing on solid ground
 * @param result_velocity Output: modified velocity after collision
 */
void Physics_MoveWithCollision(const World *world, Vector3 *position, Vector3 velocity,
                               BoundingBox bounds, float dt, bool *on_ground,
                               Vector3 *result_velocity, PhysicsCollisionInfo *collision_info);

/**
 * Creates an AABB at the given position with specified dimensions.
 *
 * @param position  Center-bottom position of the bounding box
 * @param half_size Half-width in each direction (for player: 0.3, 0, 0.3)
 * @param height    Height of the bounding box
 * @return          The computed axis-aligned bounding box
 */
BoundingBox Player_GetBoundsAtPosition(Vector3 position, Vector3 half_size, float height);

#endif /* RLVOXEL_PHYSICS_H */
