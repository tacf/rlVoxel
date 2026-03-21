# UI Tech Details

Game UI is built on a separate custom built Library Framework that's highly inspired by the amazing job that
Nic Barker has done with [Clay](https://github.com/nicbarker/clay). Initially the intention was to use that
explicitly but that would defeat the purpose of learning how to do it so i kind went the direction of "rebuilding"
some of the work Nic does. Main difference would be that this is a retained mode first library instead of 
immediate mode one like _Clay_ (Although _Clay_ does support retained mode so if you need a layout lib i highly recommend it).

## Quick Overview

The UI stack is split into two layers:

- Generic retained-mode UI library in `libui/`.
- Game-specific UI building in `src/ui/hud.*`.

In `Game_DrawHUD()` in `src/game/game.c` you can see how the UI is built:

1. Begin UI frame.
2. Build retained HUD panel content (info panel + hotbar).
  2.x Insert all logic and building function calls tree to actually build the UI
3. End UI frame (layout + state update + ordered command flush/batching).

The debug menu is separate from this stack and uses ImGui (see `game_draw_debug_ui()` in `src/game/game.c`).

## About the UI Library

Owns:

- `UiContext` lifecycle and retained tree storage.
- Layout solving (`row`/`column`, padding, margin, gap, justify, align).
- Resolution-independent size primitives (`UI_Px`, `UI_Percent`, `UI_Grow`, `UI_Vw`, `UI_Vh`).
- Draw command recording/flushing (rect/text/image).
- Text/rect/image drawing and element interaction state (`hovered`, `pressed`, `released`, `held`).

Public entrypoints:

- UI System Lifecycle main Functions: `UI_Init`, `UI_Shutdown`, `UI_BeginFrame`, `UI_EndFrame`.
- Style/size helpers: `UI_Style`, `UI_TextStyle`, `UI_Size`, `UI_SizeRange`.
- UI ree APIs: `UI_PushContainer`, `UI_PopContainer`, `UI_Text`, `UI_Rect`, `UI_Image`.
- UI Elements Query Functions: `UI_GetElementState`, `UI_IsHovered`, `UI_WasPressed`, `UI_WasReleased`, `UI_GetRect`.

## About Game UI

For now Game UI only has two main components, _Info Panel_ and the _Hotbar_:

- `HUD_BuildInfoPanel(...)` for debug text (fps/facing/xyz/biome).
- `HUD_BuildHotbar(...)` for retained hotbar node composition.


## Resolution Independent UI

`UI_BeginFrame()` computes:

- `scale_x = screen_width / reference_width`
- `scale_y = screen_height / reference_height`
- `ui->scale = min(scale_x, scale_y)` (clamped to a small positive minimum)

Reference resolution is configurable through `UI_SetReferenceResolution(...)` (currently `1280 x 720` in game init).

## Pixel-Perfect Text

To keep pixel fonts sharp and prevent weird blurry pixels when changing window size or UI scale (eventually will be a thing):

- Font texture filter is forced to `TEXTURE_FILTER_POINT`.
- Text scale uses integer snapping: `floor(ui->scale)`, clamped to `>= 1`.
- Text position is rounded to integer pixels.
- Font size and spacing are rounded to integer pixels.

This avoids sub-pixel sampling blur when window size is non-integer relative to reference size.

## Batching Notes

`UI_EndFrame()` records draw commands in strict UI order, then flushes contiguous runs:

- Contiguous rect runs are flushed together (while still using `DrawRectangleRec` per rect).
- Contiguous image runs are grouped by texture id and flushed via `DrawTexturePro`.
- Text commands remain explicit boundaries (`DrawTextEx`) in v1.

Given Raylib already has internal batching this here might as well be overengineering but the 
"batching" we're implementing here serves as a example at least.

## Hotbar Rendering Details

`HUD_BuildHotbar(...)` builds a bottom-center retained hotbar with scale-aware sizing.

For each slot we lookup the tile, apply the default tint and add it as a image to the UI.

- Atlas tile lookup: `Block_Texture(block_id, FACE_UP)`
- Atlas tile size: `16 x 16`
- Tint application: `Block_GetFaceTint(block_id, FACE_UP, ...)`
- Image node call: `UI_Image(...)` with atlas source + tint

## Possible improvements

- No clipping/masking yet.
- No text wrapping
- Full UI element tree is rebuilt every frame despite the actual "graphical objects" being reused.
- Text is still being rendered by explicit text calls instead of creating and storing glyphs to be rendered.
- Add dsl, file defined UI (Maybe?)
