#define CLAY_IMPLEMENTATION
#include "ui/clay_render.h"

#include <raylib.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Clay passes string slices; raylib's text functions want null-terminated
 * C strings. Shared between measuring and drawing so the buffer only grows
 * for unusually long text. */
static char *s_scratch = NULL;
static int s_scratch_capacity = 0;

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
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
      BeginScissorMode((int)box.x, (int)box.y, (int)box.width, (int)box.height);
      break;
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
      EndScissorMode();
      break;
    default:
      break;
    }
  }
}
