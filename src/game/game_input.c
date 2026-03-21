#include "game/game_input.h"

#include <raymath.h>

#include "raylib.h"

void Game_CaptureFrameInput(GameInputSnapshot *input) {
  if (!input) {
    return;
  }

  *input = (GameInputSnapshot){
      .move_forward = IsKeyDown(KEY_W),
      .move_backward = IsKeyDown(KEY_S),
      .move_left = IsKeyDown(KEY_A),
      .move_right = IsKeyDown(KEY_D),
      .sprint = IsKeyDown(KEY_LEFT_SHIFT),
      .jump_held = IsKeyDown(KEY_SPACE),
      .escape_pressed = IsKeyPressed(KEY_ESCAPE),
      .debug_menu_pressed = IsKeyPressed(KEY_F11),
      .left_click_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT),
      .right_click_pressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT),
      .mouse_wheel_delta = GetMouseWheelMove(),
      .mouse_delta = GetMouseDelta(),
  };
}

void Game_MergeFrameInput(GameInputSnapshot *pending, const GameInputSnapshot *frame) {
  if (!pending || !frame) {
    return;
  }

  pending->move_forward = frame->move_forward;
  pending->move_backward = frame->move_backward;
  pending->move_left = frame->move_left;
  pending->move_right = frame->move_right;
  pending->sprint = frame->sprint;
  pending->jump_held = frame->jump_held;

  pending->escape_pressed = pending->escape_pressed || frame->escape_pressed;
  pending->debug_menu_pressed = pending->debug_menu_pressed || frame->debug_menu_pressed;
  pending->left_click_pressed = pending->left_click_pressed || frame->left_click_pressed;
  pending->right_click_pressed = pending->right_click_pressed || frame->right_click_pressed;
  pending->mouse_wheel_delta += frame->mouse_wheel_delta;
  pending->mouse_delta = Vector2Add(pending->mouse_delta, frame->mouse_delta);
}

void Game_ClearTickEdgeInput(GameInputSnapshot *input) {
  if (!input) {
    return;
  }

  input->escape_pressed = false;
  input->debug_menu_pressed = false;
  input->left_click_pressed = false;
  input->right_click_pressed = false;
  input->mouse_wheel_delta = 0.0f;
  input->mouse_delta = (Vector2){0};
}
