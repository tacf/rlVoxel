#ifndef RLVOXEL_UI_FONT_ATLAS_H
#define RLVOXEL_UI_FONT_ATLAS_H

#include <raylib.h>

/* Transforms a TTF into a raylib Font using stb_truetype's em scale
 * (stbtt_ScaleForMappingEmToPixels via STBTT_POINT_SIZE) instead of
 * LoadFontEx()'s pixel-height scale, which warps pixel/bitmap fonts.
 * See https://github.com/raysan5/raylib/issues/5678.
 *
 * Output is an ordinary raylib Font (baseSize/recs/glyphs/texture), so
 * thatt we can still use DrawTextEx/MeasureTextEx/UnloadFont directly. 
 * Returns a zeroed Font on failure.
 *
 * pixelSize is the font's own design grid, e.g. 16 for unscii-16, not a
 * display size. UI code picks its own fontSize per draw call and raylib
 * scales from baseSize for free (scaleFactor = fontSize/baseSize). Not 
 * using this integer style scaling makes the fonts not properly displayed.
 * Even if with alpha threshold the font horizontal and vertical scale may
 * look scuffed. */
Font FontAtlas_Load(const char *path, int pixelSize);

#endif /* RLVOXEL_UI_FONT_ATLAS_H */
