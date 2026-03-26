#include "ui/hud.h"

#include <math.h>

#include <raymath.h>
#include <stdint.h>

#include "game/player.h"
#include "raylib.h"
#include "ui/ui.h"
#include "world/blocks.h"
#include "world/world.h"
#include "world/worldgen.h"

static float hud_scale(const UiContext *ui) {
  if (ui == NULL || ui->scale <= 0.0f) {
    return 1.0f;
  }

  float snapped = floorf(ui->scale);
  if (snapped < 1.0f) {
    snapped = 1.0f;
  }
  return snapped;
}

static float hud_ui_scale(const UiContext *ui) {
  if (ui == NULL || ui->scale <= 0.0f) {
    return 1.0f;
  }
  return ui->scale;
}

static float hud_to_ui_px(const UiContext *ui, float screen_px) {
  return screen_px / hud_ui_scale(ui);
}

static UiLength hud_px(const UiContext *ui, float screen_px) {
  return UI_Px(hud_to_ui_px(ui, screen_px));
}

static UiEdges hud_edges(const UiContext *ui, float left, float top, float right, float bottom) {
  return UI_Edges(hud_to_ui_px(ui, left), hud_to_ui_px(ui, top), hud_to_ui_px(ui, right),
                  hud_to_ui_px(ui, bottom));
}

static Rectangle hud_block_source_rect(uint8_t block_id) {
  const float atlas_tile_size = 16.0f;
  int tile = Block_Texture(block_id, FACE_UP);
  if (tile < 0) {
    tile = 0;
  }

  int tile_x = tile % 16;
  int tile_y = tile / 16;
  return (Rectangle){
      .x = (float)tile_x * atlas_tile_size,
      .y = (float)tile_y * atlas_tile_size,
      .width = atlas_tile_size,
      .height = atlas_tile_size,
  };
}

static Color hud_block_face_tint(uint8_t block_id, int face) {
  uint8_t r = 255;
  uint8_t g = 255;
  uint8_t b = 255;
  Block_GetFaceTint(block_id, face, &r, &g, &b);
  return (Color){r, g, b, 255};
}

/* Ids for hotbar hudelements, predefined to avoid building them every frame */
static const char *HUD_HOTBAR_SLOT_BORDER_IDS[PLAYER_HOTBAR_SLOTS] = {
    "hotbar_slot_border_0", "hotbar_slot_border_1", "hotbar_slot_border_2",
    "hotbar_slot_border_3", "hotbar_slot_border_4", "hotbar_slot_border_5",
    "hotbar_slot_border_6", "hotbar_slot_border_7", "hotbar_slot_border_8",
};

static const char *HUD_HOTBAR_SLOT_FILL_IDS[PLAYER_HOTBAR_SLOTS] = {
    "hotbar_slot_fill_0", "hotbar_slot_fill_1", "hotbar_slot_fill_2",
    "hotbar_slot_fill_3", "hotbar_slot_fill_4", "hotbar_slot_fill_5",
    "hotbar_slot_fill_6", "hotbar_slot_fill_7", "hotbar_slot_fill_8",
};

static const char *HUD_HOTBAR_SLOT_ICON_IDS[PLAYER_HOTBAR_SLOTS] = {
    "hotbar_slot_icon_0", "hotbar_slot_icon_1", "hotbar_slot_icon_2",
    "hotbar_slot_icon_3", "hotbar_slot_icon_4", "hotbar_slot_icon_5",
    "hotbar_slot_icon_6", "hotbar_slot_icon_7", "hotbar_slot_icon_8",
};

void HUD_BuildInfoPanel(UiContext *ui, const Player *player, const World *world) {
  if (ui == NULL || player == NULL || world == NULL) {
    return;
  }

  float fps = GetFPS();
  float yaw_deg = player->yaw * (180.0f / PI);

  int dir_idx = (int)floorf((yaw_deg + 202.5f) / 45.0f);
  dir_idx = ((dir_idx % 8) + 8) % 8;

  const char *dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  const char *biome =
      WorldGen_GetBiomeName(&world->generator, (int)player->position.x, (int)player->position.z);
  const char *mode_name =
      (player->gameplay_mode == GAMEPLAY_MODE_SURVIVAL) ? "Survival" : "Creative";

  UiStyle info_panel = UI_Style();
  info_panel.direction = UI_DIRECTION_COLUMN;
  info_panel.padding = UI_EdgesAll(8.0f);
  info_panel.margin = UI_Edges(10.0f, 20.0f, 0.0f, 0.0f);
  info_panel.gap = 3.0f;
  info_panel.draw_background = true;
  info_panel.background_color = (Color){0, 0, 0, 105};

  UI_PushContainer(ui, "hud_info", &info_panel);

  UiTextStyle text = UI_TextStyle();
  text.font_size = 20.0f;
  text.color = WHITE;

  UI_Text(ui, "fps", TextFormat("FPS: %.0f", fps), NULL, &text);
  UI_Text(ui, "facing", TextFormat("Facing: %s", dirs[dir_idx]), NULL, &text);
  UI_Text(ui, "xyz",
          TextFormat("XYZ: %.2f, %.2f, %.2f", player->position.x, player->position.y,
                     player->position.z),
          NULL, &text);
  UI_Text(ui, "mode", TextFormat("Mode: %s%s", mode_name, player->fly_enabled ? " (Fly)" : ""),
          NULL, &text);
  UI_Text(ui, "biome", TextFormat("Biome: %s", biome), NULL, &text);

  UI_PopContainer(ui);
}

void HUD_BuildHotbar(UiContext *ui, Texture2D terrain_texture, const Player *player) {
  if (ui == NULL || player == NULL) {
    return;
  }

  float scale = hud_scale(ui);
  float panel_padding = 6.0f * scale;
  float slot_size = 36.0f * scale;
  float slot_gap = 5.0f * scale;

  float total_slots_width =
      (slot_size * (float)PLAYER_HOTBAR_SLOTS) + (slot_gap * (float)(PLAYER_HOTBAR_SLOTS - 1));
  float panel_width = total_slots_width + panel_padding * 2.0f;
  float panel_height = slot_size + panel_padding * 2.0f;
  float icon_size = slot_size - (8.0f * scale);
  if (icon_size < 4.0f) {
    icon_size = 4.0f;
  }

  UiStyle overlay = UI_Style();
  overlay.width = UI_Size(UI_Percent(100.0f));
  /* Root UI is column-flow; grow keeps this overlay in the remaining viewport area. */
  overlay.height = UI_Size(UI_Grow(1.0f));
  overlay.direction = UI_DIRECTION_COLUMN;
  overlay.align_items = UI_ALIGN_CENTER;
  overlay.justify_content = UI_JUSTIFY_END;
  UI_PushContainer(ui, "hud_hotbar_overlay", &overlay);

  UiStyle panel_border = UI_Style();
  panel_border.width = UI_Size(hud_px(ui, panel_width));
  panel_border.height = UI_Size(hud_px(ui, panel_height));
  panel_border.margin = hud_edges(ui, 0.0f, 0.0f, 0.0f, 20.0f * scale);
  panel_border.padding = UI_EdgesAll(hud_to_ui_px(ui, 1.0f));
  panel_border.draw_background = true;
  panel_border.background_color = (Color){210, 210, 210, 110};
  UI_PushContainer(ui, "hud_hotbar_panel_border", &panel_border);

  UiStyle panel_fill = UI_Style();
  panel_fill.width = UI_Size(UI_Percent(100.0f));
  panel_fill.height = UI_Size(UI_Percent(100.0f));
  panel_fill.direction = UI_DIRECTION_ROW;
  panel_fill.align_items = UI_ALIGN_CENTER;
  panel_fill.padding = UI_EdgesAll(hud_to_ui_px(ui, panel_padding - 1.0f));
  panel_fill.gap = hud_to_ui_px(ui, slot_gap);
  panel_fill.draw_background = true;
  panel_fill.background_color = (Color){0, 0, 0, 140};
  UI_PushContainer(ui, "hud_hotbar_panel_fill", &panel_fill);

  for (int i = 0; i < PLAYER_HOTBAR_SLOTS; i++) {
    bool selected = (i == player->hotbar_index);
    Color slot_fill = selected ? (Color){248, 238, 190, 220} : (Color){45, 45, 45, 210};
    Color slot_border = selected ? (Color){255, 220, 100, 255} : (Color){190, 190, 190, 180};
    float slot_border_size = selected ? 2.0f : 1.0f;

    UiStyle slot_border_style = UI_Style();
    slot_border_style.width = UI_Size(hud_px(ui, slot_size));
    slot_border_style.height = UI_Size(hud_px(ui, slot_size));
    slot_border_style.padding = UI_EdgesAll(hud_to_ui_px(ui, slot_border_size));
    slot_border_style.draw_background = true;
    slot_border_style.background_color = slot_border;
    UI_PushContainer(ui, HUD_HOTBAR_SLOT_BORDER_IDS[i], &slot_border_style);

    UiStyle slot_fill_style = UI_Style();
    slot_fill_style.width = UI_Size(UI_Percent(100.0f));
    slot_fill_style.height = UI_Size(UI_Percent(100.0f));
    slot_fill_style.direction = UI_DIRECTION_COLUMN;
    slot_fill_style.align_items = UI_ALIGN_CENTER;
    slot_fill_style.justify_content = UI_JUSTIFY_CENTER;
    slot_fill_style.draw_background = true;
    slot_fill_style.background_color = slot_fill;
    UI_PushContainer(ui, HUD_HOTBAR_SLOT_FILL_IDS[i], &slot_fill_style);

    if (terrain_texture.id != 0) {
      uint8_t block_id = player->hotbar_blocks[i];
      Rectangle source = hud_block_source_rect(block_id);
      Color tint = hud_block_face_tint(block_id, FACE_UP);

      UiStyle icon_style = UI_Style();
      icon_style.width = UI_Size(hud_px(ui, icon_size));
      icon_style.height = UI_Size(hud_px(ui, icon_size));
      icon_style.align_self = UI_ALIGN_CENTER;

      UI_Image(ui, HUD_HOTBAR_SLOT_ICON_IDS[i], terrain_texture, source, tint, &icon_style);
    }

    UI_PopContainer(ui);
    UI_PopContainer(ui);
  }

  UI_PopContainer(ui);
  UI_PopContainer(ui);
  UI_PopContainer(ui);
}
