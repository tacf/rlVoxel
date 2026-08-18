# UI Tech Details

Game UI runs on [Clay](https://github.com/nicbarker/clay), a single-header
immediate-mode layout library, plus a small raylib renderer and a
retained-mode caching layer for the parts of the HUD that don't change every
frame. rlvoxel used to have its own homegrown, Clay-inspired layout library
(`libui/`); it's gone now that Clay does the same job without us having to
own a flex solver's edge cases ourselves.

## Quick Overview

The UI stack has three layers:

- Clay itself (`clay.h`, vendored via CMake `FetchContent`, pinned to `v0.14`)
  handles layout solving (row/column flex, padding, gap, alignment, sizing)
  and produces a flat `Clay_RenderCommandArray` each frame.
- A generic Clay -> raylib renderer in `src/ui/clay_render.*` turns that
  render command array into `DrawRectangleRec`/`DrawTextEx`/`DrawTexturePro`/
  scissor calls. Stateless, reusable for any Clay layout.
- Game-specific UI building in `src/ui/hud.*` declares the actual HUD
  elements (info panel + hotbar) and owns the hotbar's retained-mode texture
  cache.

`Game_DrawHUD()` in `src/game/game.c` ties it together:

1. `Clay_SetLayoutDimensions()` / `Clay_SetPointerState()` feed this frame's
   screen size and pointer state to Clay.
2. `Clay_BeginLayout()`, declare the HUD tree (`HUD_BuildInfoPanel()`,
   `HUD_BuildHotbar()`), then `Clay_EndLayout()`. This is Clay's layout pass,
   and it's unavoidably immediate mode: the whole element tree gets
   redeclared every frame or the flex solver has nothing to work with.
3. `ClayRender_Draw()` turns the resulting render commands into raylib draw
   calls.
4. `HUD_DrawHotbar()` draws the hotbar's cached background texture plus the
   per-slot block icons on top (see "Retained Hotbar background" below).

The debug menu lives outside this stack entirely and uses ImGui (see
`game_draw_debug_ui()` in `src/game/game.c`).

## Font Rendering

`src/ui/font_atlas.c` builds the game's `Font` straight from `stb_truetype`
instead of going through raylib's `LoadFontEx()`. raylib scales its glyph
atlas with `stbtt_ScaleForPixelHeight()`, which distorts pixel/bitmap-style
fonts (see [raylib#5678](https://github.com/raysan5/raylib/issues/5678)).
`FontAtlas_Load()` uses `stbtt_PackFontRange()` with `STBTT_POINT_SIZE()`
instead, scaling via `stbtt_ScaleForMappingEmToPixels()`. The result is a
plain raylib `Font` (same `baseSize`/`recs`/`glyphs`/`texture` shape
`LoadFontEx` would give you), so `DrawTextEx`/`MeasureTextEx`/`UnloadFont`
keep working untouched everywhere, including the raw pause-menu text in
`Game_DrawHUD()` that never goes through Clay at all.

Two other things matter for a pixel font like the one we ship
(`unscii-16`):

- It gets baked at its own native size (16px), not whatever size a given
  piece of UI wants to display at. Individual draw calls pick their own
  on-screen `fontSize`, and raylib scales from `baseSize` for free.
- Coverage gets thresholded to a hard on/off mask (same idea as raylib's
  `FONT_BITMAP` path) instead of left antialiased, so edges read as sharp
  pixels instead of a soft blur.

Font texture filtering stays forced to `TEXTURE_FILTER_POINT` so glyphs stay
pixel-sharp on top of all that.

## Resolution Independent UI

Clay has no built-in "reference resolution" concept; it just takes raw pixel
values. `hud_scale()` in `hud.c` reproduces the old behavior directly:

- `scale = min(screen_width / 1280, screen_height / 720)`, floored to an
  integer and clamped to `>= 1`.

All HUD sizes (panel padding, slot size, gaps) get computed in real screen
pixels from this scale and passed straight to `CLAY_SIZING_FIXED(...)`, no
extra unit conversion needed since Clay doesn't re-scale fixed sizes on its
own. Snapping the scale to an integer keeps glyphs and slot edges landing on
whole pixels, which avoids blur from a point-filtered atlas at a
non-integer scale.

## Retained Hotbar Background

Clay's layout declaration is immediate mode by design (their own docs say as
much) and they recommend mapping stable per-element ids to persistent
objects for anything that wants real retained-mode behavior. raylib's 2D
drawing has no persistent scene graph, though: every draw call has to be
reissued each frame regardless of whether anything changed, since the
framebuffer gets fully redrawn. So the only way to actually retain something
here is a persistent render target.

`HUD_BuildHotbar()` only reserves a `CLAY_SIZING_FIXED` box in the Clay tree
(`hud_hotbar_panel`) for layout purposes. `HUD_DrawHotbar()`, called after
`ClayRender_Draw()` once `Clay_GetElementData()` can resolve that box's
final screen position, bakes the panel border, fill, and per-slot
border/fill rectangles into a `RenderTexture2D`. It only regenerates that
texture when something affecting its look actually changed: selected hotbar
slot, UI scale, or panel size. Otherwise it just blits the cached texture
with a single `DrawTexturePro`. Per-slot block icons are drawn separately on
top of that blit, since Clay's built-in image element can only do a
full-texture blit (no atlas sub-rect), and icons change independently of the
background anyway.

The info panel doesn't get the same treatment: its text (fps, position,
biome) changes almost every frame, so there'd be nothing worth retaining.

## Hotbar Rendering Details

Per slot: atlas tile lookup via `Block_Texture(block_id, FACE_UP)` (tile size
`16x16`), tint via `Block_GetFaceTint(...)`, drawn with `DrawTexturePro`
straight onto the screen rather than through Clay (see above).

## Possible Improvements

- No clipping/scrolling yet, though Clay supports it (`.clip` on
  `Clay_ElementDeclaration`) whenever the HUD needs it.
- No text wrapping.
- The info panel's background rectangle is cheap enough to draw fresh every
  frame as-is; if that ever changes it could get the same retained-texture
  treatment as the hotbar, gated on whatever inputs actually move.
