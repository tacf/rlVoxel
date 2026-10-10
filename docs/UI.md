# UI

The HUD and menus use [Clay](https://github.com/nicbarker/clay) v0.14 for layout
and raylib for drawing. CMake fetches Clay.

## Layout and drawing

- `src/ui/clay_render.*`: Clay context, drawing, and nested clipping.
- `src/ui/hud.*`: info panel, hotbar layout, and hotbar cache.
- `src/ui/menu.*`: menus, buttons, and scrolling world list.

`app.c` creates one shared context with `ClayRender_Init()` and frees it with
`ClayRender_Shutdown()`. `Menu_Draw()` builds the main menu and world screens.
`Menu_BuildPause()` builds pause and options. Screen changes happen after drawing
to keep world-list text alive until the renderer is done.

`Game_DrawHUD()` in `src/game/game.c` draws the HUD in this order:

1. `ClayRender_BeginFrame()` passes screen size, font, mouse, and scrolling to Clay.
2. `Clay_BeginLayout()`, HUD declarations, then `Clay_EndLayout()` rebuild the layout.
3. `HUD_DrawHotbar()` draws the cached background and block icons.
4. `ClayRender_Draw()` draws the remaining HUD and pause menu over the hotbar.

Menu mouse input is disabled while gameplay has captured the cursor.

The debug menu uses ImGui, not Clay. See `game_draw_debug_ui()` in `src/game/game.c`.

## Menus

`src/ui/text_edit.*` stores cursor and selection positions. Left/Right and
Home/End move the cursor. Basic text selections using shift or Ctrl+A.

Menus use solid green panels, brown buttons, green accents, and cream text.
Only the pause backdrop and text selection are transparent.

## Fonts

`src/ui/font_atlas.c` builds a raylib `Font` with `stb_truetype`.
`FontAtlas_Load()` uses `stbtt_PackFontRange()` with `STBTT_POINT_SIZE()` and
`stbtt_ScaleForMappingEmToPixels()`. This avoids the pixel-font distortion from
raylib's `LoadFontEx()` (see [raylib#5678](https://github.com/raysan5/raylib/issues/5678)).

The bundled `unscii-16` font is baked at 16px and scaled when drawn. Glyph pixels
are either fully on or off, with no antialiasing. `TEXTURE_FILTER_POINT` keeps
the edges sharp when scaling.

## HUD scaling

`hud_scale()` in `hud.c` uses `min(screen_width / 1280, screen_height / 720)`,
rounded down with a minimum of 1. HUD sizes and spacing use this scale through
`CLAY_SIZING_FIXED(...)` to keep edges on whole pixels.

## Hotbar cache

`HUD_BuildHotbar()` reserves the `hud_hotbar_panel` box. After layout,
`HUD_DrawHotbar()` gets its position with `Clay_GetElementData()`.

The panel and slot backgrounds are cached in a `RenderTexture2D`. The cache is
rebuilt when the selected slot, UI scale, or panel size changes, then drawn with
one `DrawTexturePro()` call per frame.

Block icons are drawn separately because Clay images cannot select part of a
texture atlas. Each icon uses a 16x16 tile from `Block_Texture(block_id, FACE_UP)`
and a tint from `Block_GetFaceTint(...)`.

Clay draws slot numbers in the top-right corner, after the block icons.
Number keys 1–9 select the matching slot during gameplay.

The info panel is drawn each frame. FPS, position, and biome text change often.

## Not yet supported

- Keyboard navigation across all menu controls.
- Unicode text input.
