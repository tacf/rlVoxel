#define CLAY_IMPLEMENTATION
#include "ui/clay_render.h"

#include <raylib.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* Clay passes string slices; raylib's text functions want null-terminated
 * C strings. Shared between measuring and drawing so the buffer only grows
 * for unusually long text. */
static char *s_scratch = NULL;
static int s_scratch_capacity = 0;
static void *s_clay_memory;
static Rectangle *s_scissors;
static int s_scissors_capacity;

bool ClayRender_Init(Font *font) {
  uint32_t size = Clay_MinMemorySize();
  s_clay_memory = malloc(size);
  if (!s_clay_memory)
    return false;
  Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(size, s_clay_memory),
                  (Clay_Dimensions){GetScreenWidth(), GetScreenHeight()}, (Clay_ErrorHandler){0});
  Clay_SetMeasureTextFunction(ClayRender_MeasureText, font);
  return true;
}

void ClayRender_Shutdown(void) {
  Clay_SetCurrentContext(NULL);
  free(s_clay_memory);
  s_clay_memory = NULL;
  free(s_scratch);
  s_scratch = NULL;
  s_scratch_capacity = 0;
  free(s_scissors);
  s_scissors = NULL;
  s_scissors_capacity = 0;
}

void ClayRender_BeginFrame(Font *font, bool pointer_enabled) {
  Clay_SetMeasureTextFunction(ClayRender_MeasureText, font);
  Clay_SetLayoutDimensions((Clay_Dimensions){GetScreenWidth(), GetScreenHeight()});
  Vector2 pointer = pointer_enabled ? GetMousePosition() : (Vector2){-1, -1};
  Clay_SetPointerState((Clay_Vector2){pointer.x, pointer.y},
                       pointer_enabled && IsMouseButtonDown(MOUSE_LEFT_BUTTON));
  Vector2 wheel = pointer_enabled ? GetMouseWheelMoveV() : (Vector2){0};
  Clay_UpdateScrollContainers(false, (Clay_Vector2){wheel.x, wheel.y}, GetFrameTime());
}

static const char *clay_render_to_cstring(const char *chars, int32_t length) {
  int needed = length + 1;
  if (needed > s_scratch_capacity) {
    char *grown = (char *)realloc(s_scratch, (size_t)needed);
    if (grown == NULL) {
      return "";
    }
    s_scratch = grown;
    s_scratch_capacity = needed;
  }

  memcpy(s_scratch, chars, (size_t)length);
  s_scratch[length] = '\0';
  return s_scratch;
}

static Color clay_render_to_color(Clay_Color color) {
  return (Color){
      .r = (unsigned char)color.r,
      .g = (unsigned char)color.g,
      .b = (unsigned char)color.b,
      .a = (unsigned char)color.a,
  };
}

Clay_Dimensions ClayRender_MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config,
                                       void *userData) {
  Font *font = (Font *)userData;
  Font fontToUse = (font != NULL && font->texture.id != 0) ? *font : GetFontDefault();

  const char *cstr = clay_render_to_cstring(text.chars, text.length);
  Vector2 size =
      MeasureTextEx(fontToUse, cstr, (float)config->fontSize, (float)config->letterSpacing);

  return (Clay_Dimensions){.width = size.x, .height = size.y};
}

static void clay_render_draw_border(Clay_BoundingBox box, const Clay_BorderRenderData *border) {
  Color color = clay_render_to_color(border->color);

  if (border->width.left > 0) {
    DrawRectangleRec((Rectangle){box.x, box.y, (float)border->width.left, box.height}, color);
  }
  if (border->width.right > 0) {
    DrawRectangleRec((Rectangle){box.x + box.width - (float)border->width.right, box.y,
                                 (float)border->width.right, box.height},
                     color);
  }
  if (border->width.top > 0) {
    DrawRectangleRec((Rectangle){box.x, box.y, box.width, (float)border->width.top}, color);
  }
  if (border->width.bottom > 0) {
    DrawRectangleRec((Rectangle){box.x, box.y + box.height - (float)border->width.bottom, box.width,
                                 (float)border->width.bottom},
                     color);
  }
}

void ClayRender_Draw(Clay_RenderCommandArray commands, Font *font) {
  if (commands.length > s_scissors_capacity) {
    Rectangle *grown = realloc(s_scissors, (size_t)commands.length * sizeof(*s_scissors));
    if (!grown)
      return;
    s_scissors = grown;
    s_scissors_capacity = commands.length;
  }
  int clip_depth = 0;
  for (int32_t i = 0; i < commands.length; i++) {
    Clay_RenderCommand *command = Clay_RenderCommandArray_Get(&commands, i);
    Clay_BoundingBox box = command->boundingBox;

    switch (command->commandType) {
    case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
      const Clay_RectangleRenderData *rect = &command->renderData.rectangle;
      DrawRectangleRec((Rectangle){box.x, box.y, box.width, box.height},
                       clay_render_to_color(rect->backgroundColor));
      break;
    }
    case CLAY_RENDER_COMMAND_TYPE_BORDER: {
      clay_render_draw_border(box, &command->renderData.border);
      break;
    }
    case CLAY_RENDER_COMMAND_TYPE_TEXT: {
      const Clay_TextRenderData *text = &command->renderData.text;
      Font fontToUse = (font != NULL && font->texture.id != 0) ? *font : GetFontDefault();
      const char *cstr =
          clay_render_to_cstring(text->stringContents.chars, text->stringContents.length);
      DrawTextEx(fontToUse, cstr, (Vector2){box.x, box.y}, (float)text->fontSize,
                 (float)text->letterSpacing, clay_render_to_color(text->textColor));
      break;
    }
    case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
      const Clay_ImageRenderData *image = &command->renderData.image;
      Texture2D *texture = (Texture2D *)image->imageData;
      if (texture == NULL || texture->id == 0) {
        break;
      }
      /* Clay_ImageRenderData.backgroundColor doubles as the image tint, and
       * its documented default is all-zero when an element sets no tint -
       * treat that as "untinted" rather than literally drawing it invisible. */
      Color tint = clay_render_to_color(image->backgroundColor);
      if (tint.r == 0 && tint.g == 0 && tint.b == 0 && tint.a == 0) {
        tint = WHITE;
      }
      DrawTexturePro(*texture, (Rectangle){0, 0, (float)texture->width, (float)texture->height},
                     (Rectangle){box.x, box.y, box.width, box.height}, (Vector2){0}, 0.0f, tint);
      break;
    }
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
      const Clay_ClipRenderData *clip = &command->renderData.clip;
      Rectangle rect = {clip->horizontal ? box.x : 0, clip->vertical ? box.y : 0,
                        clip->horizontal ? box.width : GetScreenWidth(),
                        clip->vertical ? box.height : GetScreenHeight()};
      if (clip_depth) {
        Rectangle parent = s_scissors[clip_depth - 1];
        float right = fminf(rect.x + rect.width, parent.x + parent.width);
        float bottom = fminf(rect.y + rect.height, parent.y + parent.height);
        rect.x = fmaxf(rect.x, parent.x);
        rect.y = fmaxf(rect.y, parent.y);
        rect.width = fmaxf(0, right - rect.x);
        rect.height = fmaxf(0, bottom - rect.y);
      }
      s_scissors[clip_depth++] = rect;
      BeginScissorMode((int)rect.x, (int)rect.y, (int)rect.width, (int)rect.height);
      break;
    }
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
      if (clip_depth)
        clip_depth--;
      if (clip_depth) {
        Rectangle parent = s_scissors[clip_depth - 1];
        BeginScissorMode((int)parent.x, (int)parent.y, (int)parent.width, (int)parent.height);
      } else
        EndScissorMode();
      break;
    default:
      break;
    }
  }
  if (clip_depth)
    EndScissorMode();
}
