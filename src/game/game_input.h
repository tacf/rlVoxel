#ifndef RLVOXEL_GAME_INPUT_H
#define RLVOXEL_GAME_INPUT_H

#include "raylib.h"
#include <stdbool.h>

#include "net/protocol.h"

typedef struct GameInputSnapshot {
  bool move_forward;
  bool move_backward;
  bool move_left;
  bool move_right;
  bool sprint;
  bool jump_held;

  bool escape_pressed;
  bool debug_menu_pressed;
  bool left_click_pressed;
  bool right_click_pressed;

  float mouse_wheel_delta;
  Vector2 mouse_delta;
} GameInputSnapshot;

void Game_CaptureFrameInput(GameInputSnapshot *input);
void Game_MergeFrameInput(GameInputSnapshot *pending, const GameInputSnapshot *frame);
void Game_ClearTickEdgeInput(GameInputSnapshot *input);
void Game_BuildGameplayInputCmd(const GameInputSnapshot *input, uint32_t tick_id,
                                uint8_t selected_block, bool gameplay_enabled,
                                GameplayInputCmd *out_cmd);

#endif
