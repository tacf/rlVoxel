#include "ui/hud.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <raymath.h>

#include <clay.h>

#include "game/player.h"
#include "net/protocol.h"
#include "raylib.h"
#include "world/blocks.h"
#include "world/world.h"
#include "world/worldgen.h"

/* Helper function to keep the code cleaner. Clay uses pointers to the actual
 * strings so that's what we just set here. */
static Clay_String hud_clay_string(const char *text) {
  return (Clay_String){
      .isStaticallyAllocated = false, .length = (int32_t)strlen(text), .chars = text};
}

/* Reference-resolution scale, snapped to an integer >= 1 so pixel-font glyphs
 * (drawn through a point-filtered atlas) always land on whole screen pixels. */
static float hud_scale(void) {
  float sw = (float)GetScreenWidth();
  float sh = (float)GetScreenHeight();
  float scale = fminf(sw / 1280.0f, sh / 720.0f);
  if (scale < 0.01f) {
    scale = 0.01f;
  }
  scale = floorf(scale);
  if (scale < 1.0f) {
    scale = 1.0f;
  }
  return scale;
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

void HUD_BuildInfoPanel(const Player *player, const World *world) {
  if (player == NULL || world == NULL) {
    return;
  }

  // static: so that pointers outlive this function, since Clay needs them later.
  static char fps_text[24];
  static char facing_text[24];
  static char xyz_text[64];
  static char mode_text[32];
  static char biome_text[48];

  float fps = GetFPS();
  float yaw_deg = player->yaw * (180.0f / PI);

  /* Wrap yaw into [0, 360) first so the bucket math below never has to deal
   * with negative angles. Yaw 0 faces world +Z, which this game treats as
   * "south", hence the extra +180. */
  float compass_deg = fmodf(yaw_deg + 180.0f, 360.0f);
  if (compass_deg < 0.0f) {
    compass_deg += 360.0f;
  }
  /* 8 compass directions spaced 45 degrees apart; +22.5 (half a slice)
   * centers each direction on its bucket instead of starting at its edge. */
  int dir_idx = (int)((compass_deg + 22.5f) / 45.0f) % 8;

  const char *dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  const char *biome =
      WorldGen_GetBiomeName(&world->generator, (int)player->position.x, (int)player->position.z);
  const char *mode_name =
      (player->gameplay_mode == GAMEPLAY_MODE_SURVIVAL) ? "Survival" : "Creative";

  snprintf(fps_text, sizeof(fps_text), "FPS: %.0f", fps);
  snprintf(facing_text, sizeof(facing_text), "Facing: %s", dirs[dir_idx]);
  snprintf(xyz_text, sizeof(xyz_text), "XYZ: %.2f, %.2f, %.2f", player->position.x,
           player->position.y, player->position.z);
  snprintf(mode_text, sizeof(mode_text), "Mode: %s%s", mode_name,
           player->fly_enabled ? " (Fly)" : "");
  snprintf(biome_text, sizeof(biome_text), "Biome: %s", biome);

  Clay_TextElementConfig *text_config = CLAY_TEXT_CONFIG({
      .textColor = {255, 255, 255, 255},
      .fontSize = 20,
  });

  /* Clay has no per-element margin; offset the panel from the screen edge via
   * padding on a FIT-sized passthrough wrapper instead. */
  CLAY({
      .id = CLAY_ID("hud_info_offset"),
      .layout =
          {
              .sizing = {.width = CLAY_SIZING_FIT(0), .height = CLAY_SIZING_FIT(0)},
              .padding = {.left = 10, .top = 20},
          },
  }) {
    CLAY({
        .id = CLAY_ID("hud_info"),
        .layout =
            {
                .sizing = {.width = CLAY_SIZING_FIT(0), .height = CLAY_SIZING_FIT(0)},
                .padding = CLAY_PADDING_ALL(8),
                .childGap = 3,
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
            },
        .backgroundColor = {0, 0, 0, 105},
    }) {
      CLAY_TEXT(hud_clay_string(fps_text), text_config);
      CLAY_TEXT(hud_clay_string(facing_text), text_config);
      CLAY_TEXT(hud_clay_string(xyz_text), text_config);
      CLAY_TEXT(hud_clay_string(mode_text), text_config);
      CLAY_TEXT(hud_clay_string(biome_text), text_config);
    }
  }
}

typedef struct HudHotbarMetrics {
  float scale;
  float panel_padding;
  float slot_size;
  float slot_gap;
  float panel_width;
  float panel_height;
} HudHotbarMetrics;

static HudHotbarMetrics hud_hotbar_metrics(void) {
  HudHotbarMetrics m;
  m.scale = hud_scale();
  m.panel_padding = 6.0f * m.scale;
  m.slot_size = 36.0f * m.scale;
  m.slot_gap = 5.0f * m.scale;

  float total_slots_width =
      (m.slot_size * (float)PLAYER_HOTBAR_SLOTS) + (m.slot_gap * (float)(PLAYER_HOTBAR_SLOTS - 1));
  m.panel_width = total_slots_width + m.panel_padding * 2.0f;
  m.panel_height = m.slot_size + m.panel_padding * 2.0f;
  return m;
}

void HUD_BuildHotbar(const Player *player) {
  if (player == NULL) {
    return;
  }

  HudHotbarMetrics m = hud_hotbar_metrics();

  CLAY({
      .id = CLAY_ID("hud_hotbar_overlay"),
      .layout =
          {
              .sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
              .padding = {.bottom = (uint16_t)(20.0f * m.scale)},
              .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_BOTTOM},
          },
  }) {
    /* Background (border/fill/slots) is baked into a retained RenderTexture2D by
     * HUD_DrawHotbar(); Clay only reserves the screen position/size here. */
    CLAY({
        .id = CLAY_ID("hud_hotbar_panel"),
        .layout =
            {
                .sizing = {.width = CLAY_SIZING_FIXED(m.panel_width),
                          .height = CLAY_SIZING_FIXED(m.panel_height)},
            },
    }) {}
  }
}

/* Retained hotbar background -- Mostly as an example for now on how to do it.
 * The border/fill/slot backgrounds only change when the selected slot or the
 * UI scale changes. We build a RenderTexture2D. Clay just has the meta info
 * about the location of the hotbar mainly for handling the other elements */
typedef struct HudHotbarCache {
  RenderTexture2D texture;
  bool valid;
  int hotbar_index;
  float scale;
  int width;
  int height;
} HudHotbarCache;

static HudHotbarCache s_hotbar_cache = {0};

static void hud_draw_hotbar_background(const HudHotbarMetrics *m, const Player *player) {
  DrawRectangle(0, 0, (int)m->panel_width, (int)m->panel_height, (Color){210, 210, 210, 110});

  const float inset = 1.0f;
  DrawRectangle((int)inset, (int)inset, (int)(m->panel_width - inset * 2.0f),
                (int)(m->panel_height - inset * 2.0f), (Color){0, 0, 0, 140});

  float cursor_x = m->panel_padding;
  for (int i = 0; i < PLAYER_HOTBAR_SLOTS; i++) {
    bool selected = (i == player->hotbar_index);
    Color slot_fill = selected ? (Color){248, 238, 190, 220} : (Color){45, 45, 45, 210};
    Color slot_border = selected ? (Color){255, 220, 100, 255} : (Color){190, 190, 190, 180};
    float border_size = selected ? 2.0f : 1.0f;

    Rectangle border_rect = {cursor_x, m->panel_padding, m->slot_size, m->slot_size};
    DrawRectangleRec(border_rect, slot_border);

    Rectangle fill_rect = {
        border_rect.x + border_size,
        border_rect.y + border_size,
        border_rect.width - border_size * 2.0f,
        border_rect.height - border_size * 2.0f,
    };
    DrawRectangleRec(fill_rect, slot_fill);

    cursor_x += m->slot_size + m->slot_gap;
  }
}

void HUD_DrawHotbar(Texture2D terrain_texture, const Player *player) {
  if (player == NULL) {
    return;
  }

  HudHotbarMetrics m = hud_hotbar_metrics();
  int width = (int)ceilf(m.panel_width);
  int height = (int)ceilf(m.panel_height);
  if (width < 1) {
    width = 1;
  }
  if (height < 1) {
    height = 1;
  }

  bool dirty = !s_hotbar_cache.valid || s_hotbar_cache.width != width ||
               s_hotbar_cache.height != height || s_hotbar_cache.scale != m.scale ||
               s_hotbar_cache.hotbar_index != player->hotbar_index;

  if (dirty) {
    if (s_hotbar_cache.valid &&
        (s_hotbar_cache.width != width || s_hotbar_cache.height != height)) {
      UnloadRenderTexture(s_hotbar_cache.texture);
      s_hotbar_cache.valid = false;
    }
    if (!s_hotbar_cache.valid) {
      s_hotbar_cache.texture = LoadRenderTexture(width, height);
      s_hotbar_cache.valid = (s_hotbar_cache.texture.id != 0);
    }

    if (s_hotbar_cache.valid) {
      BeginTextureMode(s_hotbar_cache.texture);
      ClearBackground(BLANK);
      hud_draw_hotbar_background(&m, player);
      EndTextureMode();

      s_hotbar_cache.width = width;
      s_hotbar_cache.height = height;
      s_hotbar_cache.scale = m.scale;
      s_hotbar_cache.hotbar_index = player->hotbar_index;
    }
  }

  Clay_ElementData panel = Clay_GetElementData(CLAY_ID("hud_hotbar_panel"));
  if (!panel.found) {
    return;
  }
  Clay_BoundingBox box = panel.boundingBox;

  if (s_hotbar_cache.valid) {
    /* A RenderTexture2D's pixels are stored bottom-up in GPU memory (OpenGL
     * convention), so drawing it with a plain top-down source rect would
     * come out upside down. A negative source height tells DrawTexturePro
     * to sample the rows in reverse, which flips it back right-side up. */
    Rectangle src = {0, 0, (float)s_hotbar_cache.texture.texture.width,
                     -(float)s_hotbar_cache.texture.texture.height};
    Rectangle dst = {box.x, box.y, box.width, box.height};
    DrawTexturePro(s_hotbar_cache.texture.texture, src, dst, (Vector2){0}, 0.0f, WHITE);
  }

  if (terrain_texture.id == 0) {
    return;
  }

  float icon_size = m.slot_size - (8.0f * m.scale);
  if (icon_size < 4.0f) {
    icon_size = 4.0f;
  }
  float icon_offset = (m.slot_size - icon_size) * 0.5f;

  float cursor_x = box.x + m.panel_padding;
  float icon_y = box.y + m.panel_padding + icon_offset;

  for (int i = 0; i < PLAYER_HOTBAR_SLOTS; i++) {
    uint8_t block_id = player->hotbar_blocks[i];
    Rectangle source = hud_block_source_rect(block_id);
    Color tint = hud_block_face_tint(block_id, FACE_UP);

    Rectangle dest = {cursor_x + icon_offset, icon_y, icon_size, icon_size};
    DrawTexturePro(terrain_texture, source, dest, (Vector2){0}, 0.0f, tint);

    cursor_x += m.slot_size + m.slot_gap;
  }
}
