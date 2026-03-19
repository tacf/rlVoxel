#ifndef RLVOXEL_PLAYER_H
#define RLVOXEL_PLAYER_H

#include <stdbool.h>

#include <raylib.h>
#include <stdint.h>

#include "constants.h"
#include "game/game_input.h"
#include "world/world.h"

typedef struct Player {
  Vector3 position;
  Vector3 previous_position;
  Vector3 velocity;

  float yaw;
  float previous_yaw;
  float pitch;
  float previous_pitch;

  bool on_ground;
  uint8_t selected_block;
} Player;

void Player_Init(Player *player, Vector3 spawn_position);
void Player_Update(Player *player, const World *world, const GameInputSnapshot *input,
                   float tick_dt, bool mouse_look_enabled);

Vector3 Player_GetEyePosition(const Player *player);
Vector3 Player_GetLookDirection(const Player *player);
BoundingBox Player_GetBoundsAt(const Player *player, Vector3 position);

#endif
