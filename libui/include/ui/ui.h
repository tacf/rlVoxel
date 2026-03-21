#ifndef RLVOXEL_UI_H
#define RLVOXEL_UI_H

#include <stdbool.h>
#include <stdint.h>

#include <raylib.h>

#define UI_DEFAULT_MAX_NODES 1024
#define UI_DEFAULT_TEXT_CAPACITY 16384
#define UI_DEFAULT_MAX_STATES 512
#define UI_DEFAULT_MAX_DRAW_COMMANDS 4096
#define UI_MAX_STACK_DEPTH 128

/* Stable id used for per-element persistent state lookup. */
typedef uint32_t UiElementId;

/* Supported sizing modes for width/height values. */
typedef enum UiLengthKind {
  UI_LENGTH_AUTO = 0,
  UI_LENGTH_PIXELS,
  UI_LENGTH_PERCENT,
  UI_LENGTH_GROW,
  UI_LENGTH_VIEWPORT_WIDTH,
  UI_LENGTH_VIEWPORT_HEIGHT,
} UiLengthKind;

/* A single length value (kind + numeric payload). */
typedef struct UiLength {
  UiLengthKind kind;
  float value;
} UiLength;

/* Preferred/min/max sizing triplet for one axis. */
typedef struct UiSizing {
  UiLength preferred;
  UiLength min;
  UiLength max;
} UiSizing;

/* Main axis direction for container layout. */
typedef enum UiDirection {
  UI_DIRECTION_ROW = 0,
  UI_DIRECTION_COLUMN,
} UiDirection;

/* Cross/main axis alignment options. */
typedef enum UiAlign {
  UI_ALIGN_AUTO = -1,
  UI_ALIGN_START = 0,
  UI_ALIGN_CENTER,
  UI_ALIGN_END,
  UI_ALIGN_STRETCH,
} UiAlign;

/* Main-axis distribution mode for children. */
typedef enum UiJustify {
  UI_JUSTIFY_START = 0,
  UI_JUSTIFY_CENTER,
  UI_JUSTIFY_END,
} UiJustify;

/* Box edges (margin/padding). */
typedef struct UiEdges {
  float left;
  float top;
  float right;
  float bottom;
} UiEdges;

/* Element style used by containers and drawable nodes. */
typedef struct UiStyle {
  UiSizing width;
  UiSizing height;
  UiEdges margin;
  UiEdges padding;
  float gap;
  UiDirection direction;
  UiAlign align_items;
  UiAlign align_self;
  UiJustify justify_content;
  bool draw_background;
  Color background_color;
} UiStyle;

/* Text rendering style for text nodes. */
typedef struct UiTextStyle {
  float font_size;
  float spacing;
  Color color;
} UiTextStyle;

/* Interaction state exposed for one element id. */
typedef struct UiElementState {
  UiElementId id;
  Rectangle rect;
  bool hovered;
  bool pressed;
  bool released;
  bool held;
} UiElementState;

typedef struct UiNode UiNode;
typedef struct UiPersistState UiPersistState;
typedef struct UiDrawCommand UiDrawCommand;

/* Retained UI runtime context and frame scratch buffers. */
typedef struct UiContext {
  UiNode *nodes;
  int max_nodes;
  int node_count;

  int stack[UI_MAX_STACK_DEPTH];
  int stack_count;

  char *text_buffer;
  int text_capacity;
  int text_length;

  UiPersistState *states;
  int max_states;
  int state_count;

  UiDrawCommand *draw_commands;
  int max_draw_commands;
  int draw_command_count;

  uint32_t frame_index;

  Font font;
  float default_font_size;
  float reference_width;
  float reference_height;
  float scale;
  int screen_width;
  int screen_height;
} UiContext;

/**
 * Initializes the UI context buffers.
 * Uses defaults when capacity arguments are <= 0.
 */
bool UI_Init(UiContext *ui, int max_nodes, int text_capacity, int max_states);

/**
 * Releases all UI context resources.
 */
void UI_Shutdown(UiContext *ui);

/**
 * Sets the reference resolution used to compute UI scale.
 */
void UI_SetReferenceResolution(UiContext *ui, float width, float height);

/* Length helper constructors. */
UiLength UI_Auto(void);
UiLength UI_Px(float value);
UiLength UI_Percent(float value);
UiLength UI_Grow(float weight);
UiLength UI_Vw(float value);
UiLength UI_Vh(float value);

/* Sizing helper constructors. */
UiSizing UI_Size(UiLength preferred);
UiSizing UI_SizeRange(UiLength preferred, UiLength min, UiLength max);

/* Edge helper constructors. */
UiEdges UI_Edges(float left, float top, float right, float bottom);
UiEdges UI_EdgesAll(float value);
UiEdges UI_EdgesXY(float horizontal, float vertical);

/* Default style factories. */
UiStyle UI_Style(void);
UiTextStyle UI_TextStyle(void);

/**
 * Starts a new retained UI frame.
 * Must be paired with UI_EndFrame().
 */
void UI_BeginFrame(UiContext *ui, Font font, float default_font_size);

/**
 * Resolves layout, draws nodes, and updates interaction state.
 */
void UI_EndFrame(UiContext *ui);

/**
 * Pushes a container node and makes it the current parent.
 * @return Stable element id, or 0 on failure.
 */
UiElementId UI_PushContainer(UiContext *ui, const char *id, const UiStyle *style);

/**
 * Pops the current container parent (no-op at root).
 */
void UI_PopContainer(UiContext *ui);

/**
 * Adds a text node under the current container.
 * @return Stable element id, or 0 on failure.
 */
UiElementId UI_Text(UiContext *ui, const char *id, const char *text, const UiStyle *style,
                    const UiTextStyle *text_style);

/**
 * Adds a solid rectangle node under the current container.
 * @return Stable element id, or 0 on failure.
 */
UiElementId UI_Rect(UiContext *ui, const char *id, const UiStyle *style, Color color);

/**
 * Adds a textured image node under the current container.
 * @param source Source region in texture. If zero-sized, full texture is used.
 * @return Stable element id, or 0 on failure.
 */
UiElementId UI_Image(UiContext *ui, const char *id, Texture2D texture, Rectangle source, Color tint,
                     const UiStyle *style);

/**
 * Returns full element interaction state for an id, or NULL when missing.
 */
const UiElementState *UI_GetElementState(const UiContext *ui, UiElementId id);

/**
 * Returns element hovered state, true when element is hovered this frame.
 */
bool UI_IsHovered(const UiContext *ui, UiElementId id);

/**
 * Returns if true only on the specific frame the element is pressed.
 */
bool UI_WasPressed(const UiContext *ui, UiElementId id);

/**
* Returns if true only on the specific frame the element is released.
 */
bool UI_WasReleased(const UiContext *ui, UiElementId id);

/**
 * Returns element rectangle for an id, or {0} when missing.
 */
Rectangle UI_GetRect(const UiContext *ui, UiElementId id);

#endif /* RLVOXEL_UI_H */
