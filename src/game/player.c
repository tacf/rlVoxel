#include "game/player.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <raymath.h>

#include "constants.h"
#include "game/game_input.h"
#include "physics/physics.h"
#include "raylib.h"
#include "world/blocks.h"
#include "world/world.h"

/* Pitch is clamped to ~89 degrees to prevent gimbal lock */
#define PITCH_LIMIT 1.55f
#define MOUSE_SENSITIVITY 0.0023f

static int player_wrap_hotbar_index(int index) {
  if (PLAYER_HOTBAR_SLOTS <= 0) {
    return 0;
  }

  int wrapped = index % PLAYER_HOTBAR_SLOTS;
  if (wrapped < 0) {
    wrapped += PLAYER_HOTBAR_SLOTS;
  }
  return wrapped;
}

static void apply_movement_acceleration(Player *player, float input_x, float input_z,
                                        float speed_per_tick, float tick_dt, bool sprint) {
  Vector3 forward = {
      sinf(player->yaw),
      0.0f,
      cosf(player->yaw),
  };
  Vector3 right = {
      cosf(player->yaw),
      0.0f,
      -sinf(player->yaw),
  };

  Vector3 wish = {0};
  wish = Vector3Add(wish, Vector3Scale(forward, input_z));
  wish = Vector3Add(wish, Vector3Scale(right, input_x));

  if (Vector3LengthSqr(wish) <= 1e-6f) {
    return;
  }

  wish = Vector3Normalize(wish);

  float accel_per_second = speed_per_tick / tick_dt;
  if (sprint) {
    accel_per_second *= PLAYER_SPRINT_ACCEL_MULTIPLIER;
  }

  player->velocity.x += wish.x * accel_per_second;
  player->velocity.z += wish.z * accel_per_second;
}

static void try_liquid_pop_up(Player *player, const World *world, float tick_dt) {
  BoundingBox test = Player_GetBoundsAt(player, player->position);
  test.min.y += 0.6f;
  test.max.y += 0.6f;

  if (!World_CheckAABB(world, test)) {
    player->velocity.y = PLAYER_TICK_LIQUID_POP_UP / tick_dt;
  }
}

Vector3 Player_GetEyePosition(const Player *player) {
  return (Vector3){
      player->position.x,
      player->position.y + PLAYER_EYE_HEIGHT,
      player->position.z,
  };
}

Vector3 Player_GetLookDirection(const Player *player) {
  float cos_pitch = cosf(player->pitch);
  return Vector3Normalize((Vector3){
      sinf(player->yaw) * cos_pitch,
      sinf(player->pitch),
      cosf(player->yaw) * cos_pitch,
  });
}

BoundingBox Player_GetBoundsAt(const Player *player, Vector3 position) {
  (void)player;
  return Player_GetBoundsAtPosition(position, (Vector3){PLAYER_HALF_WIDTH, 0, PLAYER_HALF_WIDTH},
                                    PLAYER_HEIGHT);
}

void Player_Init(Player *player, Vector3 spawn_position) {
  static const uint8_t default_hotbar[PLAYER_HOTBAR_SLOTS] = {
      BLOCK_STONE, BLOCK_GRASS,  BLOCK_DIRT,      BLOCK_SAND, BLOCK_GRAVEL,
      BLOCK_LOG,   BLOCK_LEAVES, BLOCK_SANDSTONE, BLOCK_ICE,
  };

  player->position = spawn_position;
  player->previous_position = spawn_position;
  player->velocity = (Vector3){0};
  player->yaw = 0.0f;
  player->previous_yaw = 0.0f;
  player->pitch = 0.0f;
  player->previous_pitch = 0.0f;
  player->on_ground = false;

  memcpy(player->hotbar_blocks, default_hotbar, sizeof(default_hotbar));
  player->hotbar_index = 2; /* DIRT */
  player->selected_block = player->hotbar_blocks[player->hotbar_index];
}

void Player_ApplyHotbarScroll(Player *player, float mouse_wheel_delta) {
  if (player == NULL) {
    return;
  }

  player->hotbar_index = player_wrap_hotbar_index(player->hotbar_index);

  if (mouse_wheel_delta != 0.0f) {
    int steps = (int)floorf(fabsf(mouse_wheel_delta));
    if (steps < 1) {
      steps = 1;
    }

    int direction = (mouse_wheel_delta > 0.0f) ? -1 : 1; /* Minecraft feel */
    for (int i = 0; i < steps; i++) {
      player->hotbar_index = player_wrap_hotbar_index(player->hotbar_index + direction);
    }
  }

  player->selected_block = player->hotbar_blocks[player->hotbar_index];
}

void Player_Update(Player *player, const World *world, const GameInputSnapshot *input,
                   float tick_dt, bool mouse_look_enabled) {
  if (!input) {
    return;
  }

  player->previous_position = player->position;
  player->previous_yaw = player->yaw;
  player->previous_pitch = player->pitch;

  if (mouse_look_enabled) {
    player->yaw -= input->mouse_delta.x * MOUSE_SENSITIVITY;
    player->pitch -= input->mouse_delta.y * MOUSE_SENSITIVITY;
    player->pitch = Clamp(player->pitch, -PITCH_LIMIT, PITCH_LIMIT);
  }

  float input_x = 0.0f;
  float input_z = 0.0f;

  if (input->move_forward) {
    input_z += 1.0f;
  }
  if (input->move_backward) {
    input_z -= 1.0f;
  }
  if (input->move_right) {
    input_x -= 1.0f;
  }
  if (input->move_left) {
    input_x += 1.0f;
  }

  BoundingBox bounds = Player_GetBoundsAt(player, player->position);
  bool in_water = World_IsAABBInWater(world, bounds);
  bool in_lava = World_IsAABBInLava(world, bounds);

  if (in_water || in_lava) {
    if (input->jump_held) {
      player->velocity.y += PLAYER_TICK_LIQUID_JUMP_BOOST / tick_dt;
    }

    apply_movement_acceleration(player, input_x, input_z, PLAYER_TICK_MOVE_LIQUID, tick_dt, false);

    PhysicsCollisionInfo collision_info = {0};
    bounds = Player_GetBoundsAt(player, player->position);
    Physics_MoveWithCollision(world, &player->position, player->velocity, bounds, tick_dt,
                              &player->on_ground, &player->velocity, &collision_info);

    float drag = in_water ? PLAYER_TICK_WATER_DRAG : PLAYER_TICK_LAVA_DRAG;
    player->velocity.x *= drag;
    player->velocity.y *= drag;
    player->velocity.z *= drag;
    player->velocity.y -= PLAYER_TICK_LIQUID_GRAVITY / tick_dt;

    if (collision_info.collided_x || collision_info.collided_z) {
      try_liquid_pop_up(player, world, tick_dt);
    }
  } else {
    if (player->on_ground && input->jump_held) {
      player->velocity.y = PLAYER_TICK_JUMP_IMPULSE / tick_dt;
      player->on_ground = false;
    }

    float move_speed = player->on_ground ? PLAYER_TICK_MOVE_GROUND : PLAYER_TICK_MOVE_AIR;
    apply_movement_acceleration(player, input_x, input_z, move_speed, tick_dt, input->sprint);

    Physics_MoveWithCollision(world, &player->position, player->velocity, bounds, tick_dt,
                              &player->on_ground, &player->velocity, NULL);

    player->velocity.x *= PLAYER_TICK_XZ_DRAG;
    player->velocity.y *= PLAYER_TICK_AIR_DRAG;
    player->velocity.z *= PLAYER_TICK_XZ_DRAG;
    player->velocity.y -= PLAYER_TICK_AIR_GRAVITY / tick_dt;

    if (player->on_ground) {
      player->velocity.x *= PLAYER_TICK_GROUND_DRAG;
      player->velocity.z *= PLAYER_TICK_GROUND_DRAG;
    }
  }

  if (player->position.y < -20.0f) {
    player->position.y = 90.0f;
    player->previous_position = player->position;
    player->velocity = (Vector3){0};
  }
}
