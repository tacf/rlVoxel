#include "ui/ui.h"
#include "raylib.h"

#include <raymath.h>

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum UiNodeType {
  UI_NODE_CONTAINER = 0,
  UI_NODE_TEXT,
  UI_NODE_RECT,
  UI_NODE_IMAGE,
} UiNodeType;

typedef enum UiDrawCommandType {
  UI_DRAW_COMMAND_RECT = 0,
  UI_DRAW_COMMAND_TEXT,
  UI_DRAW_COMMAND_IMAGE,
} UiDrawCommandType;

typedef struct UiResolvedSize {
  float value;
  bool is_grow;
  float grow_weight;
} UiResolvedSize;

typedef struct UiInputSnapshot {
  Vector2 mouse_position;
  bool pressed;
  bool released;
  bool held;
} UiInputSnapshot;

typedef struct UiDrawRectCommand {
  Rectangle rect;
  Color color;
} UiDrawRectCommand;

typedef struct UiDrawTextCommand {
  int text_offset;
  Vector2 position;
  float font_size;
  float spacing;
  Color color;
} UiDrawTextCommand;

typedef struct UiDrawImageCommand {
  Texture2D texture;
  Rectangle source;
  Rectangle dest;
  Color tint;
} UiDrawImageCommand;

struct UiDrawCommand {
  UiDrawCommandType type;
  union {
    UiDrawRectCommand rect;
    UiDrawTextCommand text;
    UiDrawImageCommand image;
  } data;
};

struct UiNode {
  UiNodeType type;
  UiElementId id;
  int parent;
  int first_child;
  int last_child;
  int next_sibling;

  UiStyle style;
  UiTextStyle text_style;
  Color color;

  Texture2D image_texture;
  Rectangle image_source;
  Color image_tint;

  int text_offset;
  Vector2 measured_text;
  Rectangle rect;
};

struct UiPersistState {
  UiElementState state;
  uint32_t last_frame_touched;
};

#ifndef UI_DEBUG_LOGS
#define UI_DEBUG_LOGS 0
#endif

/* Allocate and clear memory through raylib's allocator path. */
static void *ui_alloc_zero(size_t count, size_t item_size) {
  if (count == 0 || item_size == 0) {
    return NULL;
  }

  if (count > ((size_t)-1) / item_size) {
    return NULL;
  }

  size_t total_size = count * item_size;
  if (total_size > (size_t)UINT_MAX) {
    return NULL;
  }

  void *ptr = MemAlloc((unsigned int)total_size);
  if (ptr == NULL) {
    return NULL;
  }

  memset(ptr, 0, total_size);
  return ptr;
}

static Vector2 ui_measure_auto_size(UiContext *ui, int node_index, float available_width,
                                    float available_height);
static void ui_layout_node(UiContext *ui, int node_index, Rectangle rect);
static void ui_record_node(UiContext *ui, int node_index, const UiInputSnapshot *input);

static void ui_flush_draw_commands(UiContext *ui);
static void ui_emit_draw_command(UiContext *ui, const UiDrawCommand *command);

static float ui_maxf(float a, float b) { return (a > b) ? a : b; }

static float ui_minf(float a, float b) { return (a < b) ? a : b; }

static float ui_scale_pixels(const UiContext *ui, float value) {
  if (ui == NULL) {
    return value;
  }
  return value * ui->scale;
}

static float ui_scale_text_factor(const UiContext *ui) {
  if (ui == NULL) {
    return 1.0f;
  }

  float snapped = floorf(ui->scale);
  if (snapped < 1.0f) {
    snapped = 1.0f;
  }
  return snapped;
}

static float ui_scale_text_pixels(const UiContext *ui, float value) {
  return value * ui_scale_text_factor(ui);
}

static float ui_round_to_pixel(float value) { return floorf(value + 0.5f); }

static UiEdges ui_scale_edges(const UiContext *ui, UiEdges edges) {
  return (UiEdges){
      .left = ui_scale_pixels(ui, edges.left),
      .top = ui_scale_pixels(ui, edges.top),
      .right = ui_scale_pixels(ui, edges.right),
      .bottom = ui_scale_pixels(ui, edges.bottom),
  };
}

/* FNV-1a hash scoped by parent id for stable per-container ids. */
static UiElementId ui_hash_id(UiElementId parent_id, const char *id) {
  uint32_t hash = (parent_id == 0u) ? 2166136261u : parent_id;
  const unsigned char *text = (const unsigned char *)(id != NULL ? id : "");

  while (*text != '\0') {
    hash ^= *text++;
    hash *= 16777619u;
  }

  if (hash == 0u) {
    hash = 1u;
  }
  return hash;
}

/* Convert abstract length values into screen-space units. */
static float ui_resolve_length(const UiContext *ui, UiLength length, float parent_size,
                               bool horizontal, float auto_value) {
  float value = auto_value;

  switch (length.kind) {
  case UI_LENGTH_AUTO:
    value = auto_value;
    break;
  case UI_LENGTH_PIXELS:
    value = ui_scale_pixels(ui, length.value);
    break;
  case UI_LENGTH_PERCENT:
    value = parent_size * (length.value * 0.01f);
    break;
  case UI_LENGTH_GROW:
    value = auto_value;
    break;
  case UI_LENGTH_VIEWPORT_WIDTH:
    value = (float)ui->screen_width * (length.value * 0.01f);
    break;
  case UI_LENGTH_VIEWPORT_HEIGHT:
    value = (float)ui->screen_height * (length.value * 0.01f);
    break;
  default:
    value = auto_value;
    break;
  }

  if (!isfinite(value)) {
    value = 0.0f;
  }

  (void)horizontal;
  return value;
}

static UiResolvedSize ui_resolve_sizing(const UiContext *ui, UiSizing sizing, float parent_size,
                                        bool horizontal, float auto_value) {
  UiResolvedSize resolved = {0};
  resolved.is_grow = (sizing.preferred.kind == UI_LENGTH_GROW);
  resolved.grow_weight = resolved.is_grow ? ui_maxf(sizing.preferred.value, 1.0f) : 0.0f;

  float value = ui_resolve_length(ui, sizing.preferred, parent_size, horizontal, auto_value);
  float min_value = -FLT_MAX;
  float max_value = FLT_MAX;

  if (sizing.min.kind != UI_LENGTH_AUTO) {
    min_value = ui_resolve_length(ui, sizing.min, parent_size, horizontal, 0.0f);
  }
  if (sizing.max.kind != UI_LENGTH_AUTO) {
    max_value = ui_resolve_length(ui, sizing.max, parent_size, horizontal, FLT_MAX);
  }

  if (max_value < min_value) {
    max_value = min_value;
  }

  value = Clamp(value, min_value, max_value);
  if (value < 0.0f) {
    value = 0.0f;
  }

  resolved.value = value;
  return resolved;
}

static UiPersistState *ui_get_or_create_state(UiContext *ui, UiElementId id) {
  if (ui == NULL) {
    return NULL;
  }

  for (int i = 0; i < ui->state_count; i++) {
    if (ui->states[i].state.id == id) {
      return &ui->states[i];
    }
  }

  if (ui->state_count >= ui->max_states) {
    return NULL;
  }

  UiPersistState *state = &ui->states[ui->state_count++];
  *state = (UiPersistState){0};
  state->state.id = id;
  return state;
}

static const UiPersistState *ui_get_state_const(const UiContext *ui, UiElementId id) {
  if (ui == NULL) {
    return NULL;
  }

  for (int i = 0; i < ui->state_count; i++) {
    if (ui->states[i].state.id == id) {
      return &ui->states[i];
    }
  }

  return NULL;
}

static int ui_store_text(UiContext *ui, const char *text) {
  if (ui == NULL || ui->text_buffer == NULL || ui->text_capacity <= 0) {
    return -1;
  }

  if (text == NULL) {
    text = "";
  }

  int length = (int)TextLength(text);
  if (length < 0) {
    length = 0;
  }

  int remaining = ui->text_capacity - ui->text_length;
  if (remaining <= 0) {
    return -1;
  }

  if (length >= remaining) {
    length = remaining - 1;
  }

  if (length < 0) {
    return -1;
  }

  int offset = ui->text_length;
  memcpy(ui->text_buffer + ui->text_length, text, (size_t)length);
  ui->text_length += length;
  ui->text_buffer[ui->text_length++] = '\0';
  return offset;
}

static int ui_add_node(UiContext *ui, UiNodeType type, const char *id, const UiStyle *style) {
  if (ui == NULL || ui->nodes == NULL || ui->node_count >= ui->max_nodes || ui->stack_count <= 0) {
    return -1;
  }

  int parent_index = ui->stack[ui->stack_count - 1];
  if (parent_index < 0 || parent_index >= ui->node_count) {
    return -1;
  }

  UiNode *parent = &ui->nodes[parent_index];
  UiNode *node = &ui->nodes[ui->node_count];
  *node = (UiNode){0};

  node->type = type;
  node->parent = parent_index;
  node->first_child = -1;
  node->last_child = -1;
  node->next_sibling = -1;
  node->style = (style != NULL) ? *style : UI_Style();
  node->text_style = UI_TextStyle();
  node->text_offset = -1;
  node->image_tint = WHITE;

  char fallback[32];
  const char *id_text = id;
  if (id_text == NULL || id_text[0] == '\0') {
    snprintf(fallback, sizeof(fallback), "node_%d", ui->node_count);
    id_text = fallback;
  }
  node->id = ui_hash_id(parent->id, id_text);

  int index = ui->node_count++;

  if (parent->first_child < 0) {
    parent->first_child = index;
  } else {
    ui->nodes[parent->last_child].next_sibling = index;
  }
  parent->last_child = index;

  return index;
}

static int ui_count_children(const UiContext *ui, int node_index) {
  if (ui == NULL || node_index < 0 || node_index >= ui->node_count) {
    return 0;
  }

  int count = 0;
  int child = ui->nodes[node_index].first_child;
  while (child >= 0) {
    count++;
    child = ui->nodes[child].next_sibling;
  }
  return count;
}

static Vector2 ui_measure_text_node(UiContext *ui, UiNode *node) {
  if (ui == NULL || node == NULL) {
    return (Vector2){0};
  }

  const char *text = "";
  if (node->text_offset >= 0 && node->text_offset < ui->text_length) {
    text = ui->text_buffer + node->text_offset;
  }

  float font_size =
      node->text_style.font_size > 0.0f ? node->text_style.font_size : ui->default_font_size;
  float spacing = node->text_style.spacing;

  float scaled_font_size = ui_round_to_pixel(ui_scale_text_pixels(ui, font_size));
  float scaled_spacing = ui_round_to_pixel(ui_scale_text_pixels(ui, spacing));

  if (scaled_font_size <= 0.0f) {
    scaled_font_size = 1.0f;
  }
  if (scaled_spacing < 0.0f) {
    scaled_spacing = 0.0f;
  }

  Vector2 measured = MeasureTextEx(ui->font, text, scaled_font_size, scaled_spacing);
  measured.x = ui_maxf(0.0f, measured.x);
  measured.y = ui_maxf(0.0f, measured.y);
  node->measured_text = measured;
  return measured;
}

static Vector2 ui_measure_image_node(const UiNode *node) {
  if (node == NULL) {
    return (Vector2){0};
  }

  float width = fabsf(node->image_source.width);
  float height = fabsf(node->image_source.height);

  if (width <= 0.0f && node->image_texture.id != 0) {
    width = (float)node->image_texture.width;
  }
  if (height <= 0.0f && node->image_texture.id != 0) {
    height = (float)node->image_texture.height;
  }

  return (Vector2){
      .x = ui_maxf(0.0f, width),
      .y = ui_maxf(0.0f, height),
  };
}

static Vector2 ui_measure_child_outer(UiContext *ui, int child_index, float parent_content_width,
                                      float parent_content_height) {
  UiNode *child = &ui->nodes[child_index];
  UiEdges margin = ui_scale_edges(ui, child->style.margin);

  float child_available_width = ui_maxf(0.0f, parent_content_width - margin.left - margin.right);
  float child_available_height = ui_maxf(0.0f, parent_content_height - margin.top - margin.bottom);

  Vector2 auto_size =
      ui_measure_auto_size(ui, child_index, child_available_width, child_available_height);
  UiResolvedSize width =
      ui_resolve_sizing(ui, child->style.width, child_available_width, true, auto_size.x);
  UiResolvedSize height =
      ui_resolve_sizing(ui, child->style.height, child_available_height, false, auto_size.y);

  return (Vector2){
      .x = width.value + margin.left + margin.right,
      .y = height.value + margin.top + margin.bottom,
  };
}

static Vector2 ui_measure_auto_size(UiContext *ui, int node_index, float available_width,
                                    float available_height) {
  if (ui == NULL || node_index < 0 || node_index >= ui->node_count) {
    return (Vector2){0};
  }

  UiNode *node = &ui->nodes[node_index];
  if (node->type == UI_NODE_TEXT) {
    return ui_measure_text_node(ui, node);
  }
  if (node->type == UI_NODE_IMAGE) {
    return ui_measure_image_node(node);
  }

  UiEdges padding = ui_scale_edges(ui, node->style.padding);
  float content_available_width = ui_maxf(0.0f, available_width - padding.left - padding.right);
  float content_available_height = ui_maxf(0.0f, available_height - padding.top - padding.bottom);

  int child_count = ui_count_children(ui, node_index);
  if (child_count <= 0) {
    return (Vector2){
        .x = padding.left + padding.right,
        .y = padding.top + padding.bottom,
    };
  }

  float gap = ui_scale_pixels(ui, node->style.gap);
  float content_width = 0.0f;
  float content_height = 0.0f;

  if (node->style.direction == UI_DIRECTION_ROW) {
    int child = node->first_child;
    while (child >= 0) {
      Vector2 outer_size =
          ui_measure_child_outer(ui, child, content_available_width, content_available_height);
      content_width += outer_size.x;
      content_height = ui_maxf(content_height, outer_size.y);
      child = ui->nodes[child].next_sibling;
    }
    if (child_count > 1) {
      content_width += gap * (float)(child_count - 1);
    }
  } else {
    int child = node->first_child;
    while (child >= 0) {
      Vector2 outer_size =
          ui_measure_child_outer(ui, child, content_available_width, content_available_height);
      content_width = ui_maxf(content_width, outer_size.x);
      content_height += outer_size.y;
      child = ui->nodes[child].next_sibling;
    }
    if (child_count > 1) {
      content_height += gap * (float)(child_count - 1);
    }
  }

  return (Vector2){
      .x = padding.left + padding.right + content_width,
      .y = padding.top + padding.bottom + content_height,
  };
}

/* Resolve and place children along main/cross axes (flex-like layout). */
static void ui_layout_children(UiContext *ui, int node_index) {
  UiNode *node = &ui->nodes[node_index];
  if (node->first_child < 0) {
    return;
  }

  UiEdges padding = ui_scale_edges(ui, node->style.padding);
  Rectangle content = {
      .x = node->rect.x + padding.left,
      .y = node->rect.y + padding.top,
      .width = ui_maxf(0.0f, node->rect.width - padding.left - padding.right),
      .height = ui_maxf(0.0f, node->rect.height - padding.top - padding.bottom),
  };

  bool row = (node->style.direction == UI_DIRECTION_ROW);
  float main_available = row ? content.width : content.height;
  float cross_available = row ? content.height : content.width;
  float gap = ui_scale_pixels(ui, node->style.gap);

  int child_count = ui_count_children(ui, node_index);
  float total_main = 0.0f;
  float total_grow_weight = 0.0f;

  for (int child = node->first_child; child >= 0; child = ui->nodes[child].next_sibling) {
    UiNode *child_node = &ui->nodes[child];
    UiEdges margin = ui_scale_edges(ui, child_node->style.margin);
    float margin_main_before = row ? margin.left : margin.top;
    float margin_main_after = row ? margin.right : margin.bottom;

    float child_available_width = ui_maxf(0.0f, content.width - margin.left - margin.right);
    float child_available_height = ui_maxf(0.0f, content.height - margin.top - margin.bottom);
    Vector2 auto_size =
        ui_measure_auto_size(ui, child, child_available_width, child_available_height);

    UiResolvedSize main_size =
        row ? ui_resolve_sizing(ui, child_node->style.width, main_available, true, auto_size.x)
            : ui_resolve_sizing(ui, child_node->style.height, main_available, false, auto_size.y);

    total_main += margin_main_before + main_size.value + margin_main_after;
    if (main_size.is_grow) {
      total_grow_weight += main_size.grow_weight;
    }
  }

  if (child_count > 1) {
    total_main += gap * (float)(child_count - 1);
  }

  float remaining_main = ui_maxf(0.0f, main_available - total_main);
  float used_main = total_main;
  if (total_grow_weight > 0.0f) {
    used_main += remaining_main;
  }

  float main_start = row ? content.x : content.y;
  switch (node->style.justify_content) {
  case UI_JUSTIFY_CENTER:
    main_start += (main_available - used_main) * 0.5f;
    break;
  case UI_JUSTIFY_END:
    main_start += (main_available - used_main);
    break;
  case UI_JUSTIFY_START:
  default:
    break;
  }

  float cursor = main_start;
  for (int child = node->first_child; child >= 0; child = ui->nodes[child].next_sibling) {
    UiNode *child_node = &ui->nodes[child];
    UiEdges margin = ui_scale_edges(ui, child_node->style.margin);
    float margin_main_before = row ? margin.left : margin.top;
    float margin_main_after = row ? margin.right : margin.bottom;
    float margin_cross_before = row ? margin.top : margin.left;
    float margin_cross_after = row ? margin.bottom : margin.right;

    float child_available_width = ui_maxf(0.0f, content.width - margin.left - margin.right);
    float child_available_height = ui_maxf(0.0f, content.height - margin.top - margin.bottom);
    Vector2 auto_size =
        ui_measure_auto_size(ui, child, child_available_width, child_available_height);

    UiResolvedSize main_size =
        row ? ui_resolve_sizing(ui, child_node->style.width, main_available, true, auto_size.x)
            : ui_resolve_sizing(ui, child_node->style.height, main_available, false, auto_size.y);

    float child_main = main_size.value;
    if (main_size.is_grow && total_grow_weight > 0.0f) {
      child_main += remaining_main * (main_size.grow_weight / total_grow_weight);
    }

    UiResolvedSize cross_size =
        row ? ui_resolve_sizing(ui, child_node->style.height, cross_available, false, auto_size.y)
            : ui_resolve_sizing(ui, child_node->style.width, cross_available, true, auto_size.x);

    UiAlign align = child_node->style.align_self;
    if (align == UI_ALIGN_AUTO) {
      align = node->style.align_items;
    }
    if (align == UI_ALIGN_AUTO) {
      align = UI_ALIGN_START;
    }
    if (cross_size.is_grow) {
      align = UI_ALIGN_STRETCH;
    }

    float child_cross_space =
        ui_maxf(0.0f, cross_available - margin_cross_before - margin_cross_after);
    float child_cross = cross_size.value;
    if (align == UI_ALIGN_STRETCH) {
      child_cross = child_cross_space;
    } else {
      child_cross = ui_minf(child_cross, child_cross_space);
    }

    float cross_start = row ? content.y : content.x;
    float child_cross_pos = cross_start + margin_cross_before;
    switch (align) {
    case UI_ALIGN_CENTER:
      child_cross_pos = cross_start + (cross_available - child_cross) * 0.5f +
                        (margin_cross_before - margin_cross_after) * 0.5f;
      break;
    case UI_ALIGN_END:
      child_cross_pos = cross_start + cross_available - child_cross - margin_cross_after;
      break;
    case UI_ALIGN_STRETCH:
    case UI_ALIGN_START:
    case UI_ALIGN_AUTO:
    default:
      child_cross_pos = cross_start + margin_cross_before;
      break;
    }

    cursor += margin_main_before;
    Rectangle child_rect = {0};
    if (row) {
      child_rect.x = cursor;
      child_rect.y = child_cross_pos;
      child_rect.width = child_main;
      child_rect.height = child_cross;
    } else {
      child_rect.x = child_cross_pos;
      child_rect.y = cursor;
      child_rect.width = child_cross;
      child_rect.height = child_main;
    }

    ui_layout_node(ui, child, child_rect);
    cursor += child_main + margin_main_after + gap;
  }
}

static void ui_layout_node(UiContext *ui, int node_index, Rectangle rect) {
  if (ui == NULL || node_index < 0 || node_index >= ui->node_count) {
    return;
  }

  UiNode *node = &ui->nodes[node_index];
  node->rect = rect;
  ui_layout_children(ui, node_index);
}

static void ui_update_state(UiContext *ui, const UiNode *node, const UiInputSnapshot *input) {
  if (ui == NULL || node == NULL || input == NULL) {
    return;
  }

  UiPersistState *state = ui_get_or_create_state(ui, node->id);
  if (state == NULL) {
    return;
  }

  bool hovered = CheckCollisionPointRec(input->mouse_position, node->rect);
  state->state.rect = node->rect;
  state->state.hovered = hovered;
  state->state.pressed = hovered && input->pressed;
  state->state.released = hovered && input->released;
  state->state.held = hovered && input->held;
  state->last_frame_touched = ui->frame_index;
}

static void ui_emit_rect_command(UiContext *ui, Rectangle rect, Color color) {
  UiDrawCommand command = {
      .type = UI_DRAW_COMMAND_RECT,
      .data.rect =
          {
              .rect = rect,
              .color = color,
          },
  };
  ui_emit_draw_command(ui, &command);
}

static void ui_emit_text_command(UiContext *ui, const UiNode *node) {
  const char *text = "";
  if (node->text_offset >= 0 && node->text_offset < ui->text_length) {
    text = ui->text_buffer + node->text_offset;
  }

  float font_size =
      node->text_style.font_size > 0.0f ? node->text_style.font_size : ui->default_font_size;
  float spacing = node->text_style.spacing;

  float draw_size = ui_round_to_pixel(ui_scale_text_pixels(ui, font_size));
  float draw_spacing = ui_round_to_pixel(ui_scale_text_pixels(ui, spacing));
  if (draw_size < 1.0f) {
    draw_size = 1.0f;
  }
  if (draw_spacing < 0.0f) {
    draw_spacing = 0.0f;
  }

  Vector2 draw_pos = {ui_round_to_pixel(node->rect.x), ui_round_to_pixel(node->rect.y)};
  (void)text;

  UiDrawCommand command = {
      .type = UI_DRAW_COMMAND_TEXT,
      .data.text =
          {
              .text_offset = node->text_offset,
              .position = draw_pos,
              .font_size = draw_size,
              .spacing = draw_spacing,
              .color = node->text_style.color,
          },
  };
  ui_emit_draw_command(ui, &command);
}

static void ui_emit_image_command(UiContext *ui, const UiNode *node) {
  if (node->image_texture.id == 0) {
    return;
  }

  Rectangle dest = {
      .x = ui_round_to_pixel(node->rect.x),
      .y = ui_round_to_pixel(node->rect.y),
      .width = ui_round_to_pixel(node->rect.width),
      .height = ui_round_to_pixel(node->rect.height),
  };

  if (dest.width <= 0.0f || dest.height <= 0.0f) {
    return;
  }

  UiDrawCommand command = {
      .type = UI_DRAW_COMMAND_IMAGE,
      .data.image =
          {
              .texture = node->image_texture,
              .source = node->image_source,
              .dest = dest,
              .tint = node->image_tint,
          },
  };
  ui_emit_draw_command(ui, &command);
}

static void ui_record_node(UiContext *ui, int node_index, const UiInputSnapshot *input) {
  if (ui == NULL || node_index < 0 || node_index >= ui->node_count) {
    return;
  }

  UiNode *node = &ui->nodes[node_index];
  ui_update_state(ui, node, input);

  if (node->style.draw_background) {
    ui_emit_rect_command(ui, node->rect, node->style.background_color);
  }

  if (node->type == UI_NODE_RECT) {
    ui_emit_rect_command(ui, node->rect, node->color);
  } else if (node->type == UI_NODE_TEXT) {
    ui_emit_text_command(ui, node);
  } else if (node->type == UI_NODE_IMAGE) {
    ui_emit_image_command(ui, node);
  }

  for (int child = node->first_child; child >= 0; child = ui->nodes[child].next_sibling) {
    ui_record_node(ui, child, input);
  }
}

static void ui_draw_command_immediate(UiContext *ui, const UiDrawCommand *command) {
  if (ui == NULL || command == NULL) {
    return;
  }

  if (command->type == UI_DRAW_COMMAND_RECT) {
    DrawRectangleRec(command->data.rect.rect, command->data.rect.color);
    return;
  }

  if (command->type == UI_DRAW_COMMAND_IMAGE) {
    if (command->data.image.texture.id == 0) {
      return;
    }
    DrawTexturePro(command->data.image.texture, command->data.image.source,
                   command->data.image.dest, (Vector2){0}, 0.0f, command->data.image.tint);
    return;
  }

  if (command->type == UI_DRAW_COMMAND_TEXT) {
    const char *text = "";
    if (command->data.text.text_offset >= 0 && command->data.text.text_offset < ui->text_length) {
      text = ui->text_buffer + command->data.text.text_offset;
    }

    DrawTextEx(ui->font, text, command->data.text.position, command->data.text.font_size,
               command->data.text.spacing, command->data.text.color);
  }
}

static void ui_flush_rect_command_run(const UiDrawCommand *commands, int start, int end) {
  if (commands == NULL || start >= end) {
    return;
  }

  /*
   * Route through raylib's rectangle API for robustness across renderer states.
   * raylib still batches internally by texture/state, so this keeps behavior stable
   * while preserving command-order flushing.
   */
  for (int i = start; i < end; i++) {
    DrawRectangleRec(commands[i].data.rect.rect, commands[i].data.rect.color);
  }
}

static void ui_flush_image_command_run(const UiDrawCommand *commands, int start, int end) {
  if (commands == NULL || start >= end) {
    return;
  }

  /*
   * Keep image submission on DrawTexturePro() so atlas sampling, source-rect handling,
   * and blend state match the rest of the project exactly.
   */
  for (int i = start; i < end; i++) {
    if (commands[i].data.image.texture.id == 0) {
      continue;
    }

    DrawTexturePro(commands[i].data.image.texture, commands[i].data.image.source,
                   commands[i].data.image.dest, (Vector2){0}, 0.0f, commands[i].data.image.tint);
  }
}

static void ui_flush_draw_commands(UiContext *ui) {
  if (ui == NULL || ui->draw_commands == NULL || ui->draw_command_count <= 0) {
    return;
  }

  int index = 0;
  while (index < ui->draw_command_count) {
    UiDrawCommand *command = &ui->draw_commands[index];

    /* Flush contiguous command runs to minimize per-command branch overhead. */
    if (command->type == UI_DRAW_COMMAND_RECT) {
      int start = index;
      while (index < ui->draw_command_count &&
             ui->draw_commands[index].type == UI_DRAW_COMMAND_RECT) {
        index++;
      }
      ui_flush_rect_command_run(ui->draw_commands, start, index);
      continue;
    }

    if (command->type == UI_DRAW_COMMAND_IMAGE) {
      int start = index;
      unsigned int texture_id = command->data.image.texture.id;
      while (index < ui->draw_command_count &&
             ui->draw_commands[index].type == UI_DRAW_COMMAND_IMAGE &&
             ui->draw_commands[index].data.image.texture.id == texture_id) {
        index++;
      }
      ui_flush_image_command_run(ui->draw_commands, start, index);
      continue;
    }

    ui_draw_command_immediate(ui, command);
    index++;
  }

  ui->draw_command_count = 0;
}

static void ui_emit_draw_command(UiContext *ui, const UiDrawCommand *command) {
  if (ui == NULL || command == NULL) {
    return;
  }

  if (ui->draw_commands == NULL || ui->max_draw_commands <= 0) {
    ui_draw_command_immediate(ui, command);
    return;
  }

  if (ui->draw_command_count >= ui->max_draw_commands) {
#if UI_DEBUG_LOGS
    TraceLog(LOG_WARNING, "[libui] draw command buffer full, flushing mid-frame");
#endif
    ui_flush_draw_commands(ui);
  }

  if (ui->draw_command_count < ui->max_draw_commands) {
    ui->draw_commands[ui->draw_command_count++] = *command;
  } else {
    ui_draw_command_immediate(ui, command);
  }
}

bool UI_Init(UiContext *ui, int max_nodes, int text_capacity, int max_states) {
  if (ui == NULL) {
    return false;
  }

  *ui = (UiContext){0};

  ui->max_nodes = (max_nodes > 0) ? max_nodes : UI_DEFAULT_MAX_NODES;
  ui->text_capacity = (text_capacity > 0) ? text_capacity : UI_DEFAULT_TEXT_CAPACITY;
  ui->max_states = (max_states > 0) ? max_states : UI_DEFAULT_MAX_STATES;

  long long draw_capacity = (long long)ui->max_nodes * 8LL;
  if (draw_capacity < UI_DEFAULT_MAX_DRAW_COMMANDS) {
    draw_capacity = UI_DEFAULT_MAX_DRAW_COMMANDS;
  }
  if (draw_capacity > INT_MAX) {
    draw_capacity = INT_MAX;
  }
  ui->max_draw_commands = (int)draw_capacity;

  ui->nodes = (UiNode *)ui_alloc_zero((size_t)ui->max_nodes, sizeof(UiNode));
  ui->text_buffer = (char *)ui_alloc_zero((size_t)ui->text_capacity, sizeof(char));
  ui->states = (UiPersistState *)ui_alloc_zero((size_t)ui->max_states, sizeof(UiPersistState));
  ui->draw_commands =
      (UiDrawCommand *)ui_alloc_zero((size_t)ui->max_draw_commands, sizeof(UiDrawCommand));

  if (ui->nodes == NULL || ui->text_buffer == NULL || ui->states == NULL ||
      ui->draw_commands == NULL) {
    UI_Shutdown(ui);
    return false;
  }

  ui->reference_width = 1280.0f;
  ui->reference_height = 720.0f;
  ui->default_font_size = 16.0f;
  ui->scale = 1.0f;
  return true;
}

void UI_Shutdown(UiContext *ui) {
  if (ui == NULL) {
    return;
  }

  MemFree(ui->nodes);
  MemFree(ui->text_buffer);
  MemFree(ui->states);
  MemFree(ui->draw_commands);
  *ui = (UiContext){0};
}

void UI_SetReferenceResolution(UiContext *ui, float width, float height) {
  if (ui == NULL) {
    return;
  }

  if (width > 0.0f) {
    ui->reference_width = width;
  }
  if (height > 0.0f) {
    ui->reference_height = height;
  }
}

UiLength UI_Auto(void) { return (UiLength){.kind = UI_LENGTH_AUTO, .value = 0.0f}; }

UiLength UI_Px(float value) { return (UiLength){.kind = UI_LENGTH_PIXELS, .value = value}; }

UiLength UI_Percent(float value) { return (UiLength){.kind = UI_LENGTH_PERCENT, .value = value}; }

UiLength UI_Grow(float weight) { return (UiLength){.kind = UI_LENGTH_GROW, .value = weight}; }

UiLength UI_Vw(float value) {
  return (UiLength){
      .kind = UI_LENGTH_VIEWPORT_WIDTH,
      .value = value,
  };
}

UiLength UI_Vh(float value) {
  return (UiLength){
      .kind = UI_LENGTH_VIEWPORT_HEIGHT,
      .value = value,
  };
}

UiSizing UI_Size(UiLength preferred) {
  return (UiSizing){
      .preferred = preferred,
      .min = UI_Auto(),
      .max = UI_Auto(),
  };
}

UiSizing UI_SizeRange(UiLength preferred, UiLength min, UiLength max) {
  return (UiSizing){
      .preferred = preferred,
      .min = min,
      .max = max,
  };
}

UiEdges UI_Edges(float left, float top, float right, float bottom) {
  return (UiEdges){
      .left = left,
      .top = top,
      .right = right,
      .bottom = bottom,
  };
}

UiEdges UI_EdgesAll(float value) {
  return (UiEdges){
      .left = value,
      .top = value,
      .right = value,
      .bottom = value,
  };
}

UiEdges UI_EdgesXY(float horizontal, float vertical) {
  return (UiEdges){
      .left = horizontal,
      .right = horizontal,
      .top = vertical,
      .bottom = vertical,
  };
}

UiStyle UI_Style(void) {
  return (UiStyle){
      .width = UI_Size(UI_Auto()),
      .height = UI_Size(UI_Auto()),
      .margin = UI_EdgesAll(0.0f),
      .padding = UI_EdgesAll(0.0f),
      .gap = 0.0f,
      .direction = UI_DIRECTION_COLUMN,
      .align_items = UI_ALIGN_START,
      .align_self = UI_ALIGN_AUTO,
      .justify_content = UI_JUSTIFY_START,
      .draw_background = false,
      .background_color = BLANK,
  };
}

UiTextStyle UI_TextStyle(void) {
  return (UiTextStyle){
      .font_size = 16.0f,
      .spacing = 0.0f,
      .color = WHITE,
  };
}

void UI_BeginFrame(UiContext *ui, Font font, float default_font_size) {
  if (ui == NULL || ui->nodes == NULL || ui->text_buffer == NULL || ui->states == NULL ||
      ui->max_nodes <= 0) {
    return;
  }

  /* Reset frame-local buffers while keeping persistent interaction state. */
  ui->frame_index++;
  ui->node_count = 0;
  ui->text_length = 0;
  ui->stack_count = 0;
  ui->draw_command_count = 0;

  ui->screen_width = GetScreenWidth();
  ui->screen_height = GetScreenHeight();
  if (ui->screen_width <= 0) {
    ui->screen_width = 1;
  }
  if (ui->screen_height <= 0) {
    ui->screen_height = 1;
  }

  /* Resolution-independent scale anchored to reference resolution. */
  float scale_x = (float)ui->screen_width / ui_maxf(1.0f, ui->reference_width);
  float scale_y = (float)ui->screen_height / ui_maxf(1.0f, ui->reference_height);
  ui->scale = ui_maxf(0.01f, ui_minf(scale_x, scale_y));

  ui->font = (font.texture.id != 0) ? font : GetFontDefault();
  if (ui->font.texture.id != 0) {
    SetTextureFilter(ui->font.texture, TEXTURE_FILTER_POINT);
  }
  if (default_font_size > 0.0f) {
    ui->default_font_size = default_font_size;
  }

  /* Edge-triggered state flags are valid for one frame only. */
  for (int i = 0; i < ui->state_count; i++) {
    ui->states[i].state.hovered = false;
    ui->states[i].state.pressed = false;
    ui->states[i].state.released = false;
    ui->states[i].state.held = false;
  }

  UiNode *root = &ui->nodes[0];
  *root = (UiNode){
      .type = UI_NODE_CONTAINER,
      .id = ui_hash_id(2166136261u, "root"),
      .parent = -1,
      .first_child = -1,
      .last_child = -1,
      .next_sibling = -1,
      .style = UI_Style(),
      .text_style = UI_TextStyle(),
      .text_offset = -1,
      .rect = {0, 0, (float)ui->screen_width, (float)ui->screen_height},
  };
  root->style.width = UI_Size(UI_Percent(100.0f));
  root->style.height = UI_Size(UI_Percent(100.0f));

  ui->node_count = 1;
  ui->stack[0] = 0;
  ui->stack_count = 1;
}

void UI_EndFrame(UiContext *ui) {
  if (ui == NULL || ui->node_count <= 0) {
    return;
  }

  if (ui->stack_count <= 0) {
    return;
  }

  ui->stack_count = 1;

  /* Layout starts from a full-screen root each frame. */
  Rectangle root_rect = {
      .x = 0.0f,
      .y = 0.0f,
      .width = (float)ui->screen_width,
      .height = (float)ui->screen_height,
  };
  ui_layout_node(ui, 0, root_rect);

  /* Snapshot mouse input once so all nodes evaluate against the same state. */
  UiInputSnapshot input = {
      .mouse_position = GetMousePosition(),
      .pressed = IsMouseButtonPressed(MOUSE_LEFT_BUTTON),
      .released = IsMouseButtonReleased(MOUSE_LEFT_BUTTON),
      .held = IsMouseButtonDown(MOUSE_LEFT_BUTTON),
  };

  ui_record_node(ui, 0, &input);
  ui_flush_draw_commands(ui);
}

UiElementId UI_PushContainer(UiContext *ui, const char *id, const UiStyle *style) {
  if (ui == NULL || ui->stack_count >= UI_MAX_STACK_DEPTH) {
    return 0u;
  }

  int index = ui_add_node(ui, UI_NODE_CONTAINER, id, style);
  if (index < 0) {
    return 0u;
  }

  ui->stack[ui->stack_count++] = index;
  return ui->nodes[index].id;
}

void UI_PopContainer(UiContext *ui) {
  if (ui == NULL || ui->stack_count <= 1) {
    return;
  }

  ui->stack_count--;
}

UiElementId UI_Text(UiContext *ui, const char *id, const char *text, const UiStyle *style,
                    const UiTextStyle *text_style) {
  if (ui == NULL) {
    return 0u;
  }

  int index = ui_add_node(ui, UI_NODE_TEXT, id, style);
  if (index < 0) {
    return 0u;
  }

  UiNode *node = &ui->nodes[index];
  node->text_style = (text_style != NULL) ? *text_style : UI_TextStyle();
  node->text_offset = ui_store_text(ui, text);

  if (node->text_offset < 0) {
    node->text_offset = ui_store_text(ui, "");
  }

  return node->id;
}

UiElementId UI_Rect(UiContext *ui, const char *id, const UiStyle *style, Color color) {
  if (ui == NULL) {
    return 0u;
  }

  int index = ui_add_node(ui, UI_NODE_RECT, id, style);
  if (index < 0) {
    return 0u;
  }

  ui->nodes[index].color = color;
  return ui->nodes[index].id;
}

UiElementId UI_Image(UiContext *ui, const char *id, Texture2D texture, Rectangle source, Color tint,
                     const UiStyle *style) {
  if (ui == NULL) {
    return 0u;
  }

  int index = ui_add_node(ui, UI_NODE_IMAGE, id, style);
  if (index < 0) {
    return 0u;
  }

  UiNode *node = &ui->nodes[index];
  node->image_texture = texture;
  node->image_source = source;
  if (node->image_source.width == 0.0f || node->image_source.height == 0.0f) {
    node->image_source = (Rectangle){0.0f, 0.0f, (float)texture.width, (float)texture.height};
  }
  node->image_tint = tint;
  return node->id;
}

const UiElementState *UI_GetElementState(const UiContext *ui, UiElementId id) {
  const UiPersistState *state = ui_get_state_const(ui, id);
  if (state == NULL) {
    return NULL;
  }
  return &state->state;
}

bool UI_IsHovered(const UiContext *ui, UiElementId id) {
  const UiElementState *state = UI_GetElementState(ui, id);
  return (state != NULL) ? state->hovered : false;
}

bool UI_WasPressed(const UiContext *ui, UiElementId id) {
  const UiElementState *state = UI_GetElementState(ui, id);
  return (state != NULL) ? state->pressed : false;
}

bool UI_WasReleased(const UiContext *ui, UiElementId id) {
  const UiElementState *state = UI_GetElementState(ui, id);
  return (state != NULL) ? state->released : false;
}

Rectangle UI_GetRect(const UiContext *ui, UiElementId id) {
  const UiElementState *state = UI_GetElementState(ui, id);
  return (state != NULL) ? state->rect : (Rectangle){0};
}
