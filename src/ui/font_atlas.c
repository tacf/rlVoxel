#include "ui/font_atlas.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <raylib.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Printable ASCII: space through tilde. Covers every string this game draws
 * today (HUD, menus). Widen this if chat or non-English text ever needs it. */
#define FONT_ATLAS_FIRST_CODEPOINT ' '
#define FONT_ATLAS_LAST_CODEPOINT '~'
#define FONT_ATLAS_CODEPOINT_COUNT (FONT_ATLAS_LAST_CODEPOINT - FONT_ATLAS_FIRST_CODEPOINT + 1)
#define FONT_ATLAS_PADDING 1
#define FONT_ATLAS_MIN_SIZE 256
#define FONT_ATLAS_MAX_SIZE 2048
/* Coverage below this (out of 255) is treated as background, at/above it as
 * fully opaque ink. 80 matches raylib's own FONT_BITMAP threshold. */
#define FONT_ATLAS_ALPHA_THRESHOLD 80

Font FontAtlas_Load(const char *path, int pixelSize) {
  Font font = {0};

  if (path == NULL || pixelSize <= 0) {
    return font;
  }

  int fileSize = 0;
  unsigned char *fileData = LoadFileData(path, &fileSize);
  if (fileData == NULL) {
    return font;
  }

  stbtt_fontinfo info;
  if (!stbtt_InitFont(&info, fileData, 0)) {
    UnloadFileData(fileData);
    return font;
  }

  stbtt_packedchar *packed =
      (stbtt_packedchar *)malloc(FONT_ATLAS_CODEPOINT_COUNT * sizeof(stbtt_packedchar));
  if (packed == NULL) {
    UnloadFileData(fileData);
    return font;
  }

  unsigned char *coverage = NULL;
  int atlasSize = FONT_ATLAS_MIN_SIZE;
  int packedOk = 0;

  for (; atlasSize <= FONT_ATLAS_MAX_SIZE; atlasSize *= 2) {
    coverage = (unsigned char *)malloc((size_t)atlasSize * (size_t)atlasSize);
    if (coverage == NULL) {
      break;
    }
    memset(coverage, 0, (size_t)atlasSize * (size_t)atlasSize);

    stbtt_pack_context pc;
    stbtt_PackBegin(&pc, coverage, atlasSize, atlasSize, 0, FONT_ATLAS_PADDING, NULL);
    stbtt_PackSetOversampling(&pc, 1, 1);
    packedOk = stbtt_PackFontRange(&pc, fileData, 0, STBTT_POINT_SIZE((float)pixelSize),
                                   FONT_ATLAS_FIRST_CODEPOINT, FONT_ATLAS_CODEPOINT_COUNT, packed);
    stbtt_PackEnd(&pc);

    if (packedOk) {
      break;
    }

    free(coverage);
    coverage = NULL;
  }

  if (!packedOk || coverage == NULL) {
    UnloadFileData(fileData);
    free(coverage);
    free(packed);
    return font;
  }

  /* info points into fileData, so read metrics before freeing it below. */
  int ascent = 0;
  int descent = 0;
  int lineGap = 0;
  stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
  float scale = stbtt_ScaleForMappingEmToPixels(&info, (float)pixelSize);
  int baselineOffset = (int)roundf((float)ascent * scale);

  UnloadFileData(fileData);

  /*
   * unscii and fonts like it are TTF outlines tracing an original pixel
   * grid. Left alone, stb_truetype antialiases them like any vector font
   * and the result looks muddy instead of sharp. Threshold to a hard
   * on/off mask instead - same trick raylib's own FONT_BITMAP path uses.
   */
  for (size_t i = 0; i < (size_t)atlasSize * (size_t)atlasSize; i++) {
    coverage[i] = (coverage[i] < FONT_ATLAS_ALPHA_THRESHOLD) ? 0 : 255;
  }

  /*
   * PIXELFORMAT_UNCOMPRESSED_GRAYSCALE gets its alpha swizzled to a
   * constant GL_ONE on upload (rlgl.h), so a single-channel atlas draws
   * every glyph quad fully opaque - edge pixels that should blend in end
   * up as solid near-black blocks. GRAY_ALPHA pulls alpha from the second
   * byte instead, so just duplicate the coverage value into both.
   */
  size_t pixel_count = (size_t)atlasSize * (size_t)atlasSize;
  unsigned char *bitmap = (unsigned char *)malloc(pixel_count * 2);
  if (bitmap == NULL) {
    free(coverage);
    free(packed);
    return font;
  }
  for (size_t i = 0; i < pixel_count; i++) {
    bitmap[i * 2] = coverage[i];
    bitmap[i * 2 + 1] = coverage[i];
  }
  free(coverage);

  Rectangle *recs = (Rectangle *)malloc(FONT_ATLAS_CODEPOINT_COUNT * sizeof(Rectangle));
  GlyphInfo *glyphs = (GlyphInfo *)calloc((size_t)FONT_ATLAS_CODEPOINT_COUNT, sizeof(GlyphInfo));
  if (recs == NULL || glyphs == NULL) {
    free(bitmap);
    free(packed);
    free(recs);
    free(glyphs);
    return font;
  }

  for (int i = 0; i < FONT_ATLAS_CODEPOINT_COUNT; i++) {
    const stbtt_packedchar *pc_char = &packed[i];

    recs[i] = (Rectangle){
        .x = (float)pc_char->x0,
        .y = (float)pc_char->y0,
        .width = (float)(pc_char->x1 - pc_char->x0),
        .height = (float)(pc_char->y1 - pc_char->y0),
    };

    glyphs[i].value = FONT_ATLAS_FIRST_CODEPOINT + i;
    glyphs[i].offsetX = (int)roundf(pc_char->xoff);
    glyphs[i].offsetY = (int)roundf(pc_char->yoff) + baselineOffset;
    glyphs[i].advanceX = (int)roundf(pc_char->xadvance);
    glyphs[i].image = (Image){0};
  }

  free(packed);

  Image atlasImage = {
      .data = bitmap,
      .width = atlasSize,
      .height = atlasSize,
      .mipmaps = 1,
      .format = PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA,
  };

  font.baseSize = pixelSize;
  font.glyphCount = FONT_ATLAS_CODEPOINT_COUNT;
  font.glyphPadding = 0;
  font.texture = LoadTextureFromImage(atlasImage);
  font.recs = recs;
  font.glyphs = glyphs;

  UnloadImage(atlasImage); /* bitmap's on the GPU now, this just frees the CPU copy. */

  if (font.texture.id == 0) {
    free(recs);
    free(glyphs);
    return (Font){0};
  }

  return font;
}
