# Game Systems Tech Details

## Clouds

### Overview

Clouds use a noise based fast-cloud approach:

- Flat 2D cloud pieces (no volumetric mesh).
- Fixed cloud height (`layer_y`, default `108.5f`, the .5 is to prevent z-figthing for now).
- Constant westward drift (`speed_x < 0`).

The system is lightweight and generated once at startup.

### Data Model

Cloud runtime data lives in `Clouds` (`src/gfx/clouds.h`):

- `scroll_x`: horizontal movement offset for cloud drift.
- `speed_x`: horizontal drift speed (`-0.9f` default, westward).
- `cell_size`: world size of one cloud cell (`8.0f` default).
- `layer_y`: fixed Y level for cloud layer (`108.0f` default).
- `radius_cells`: draw radius around camera in cell units (`26` default).
- `noise_size`: generated source image size (`256` default).
- `block_px`: pixels per cloud cell in source map (`8` default).
- `grid_size`: derived map side length in cells (`noise_size / block_px`).
- `cloud_opacity`: final alpha multiplier for cloud tiles (`0.7f` default).
- `cell_map`: binary occupancy map (`0 = empty`, `1 = cloud`).

### Generation (Init)

`Clouds_Init()` generates cloud layout once:

1. `GenImagePerlinNoise()` creates a grayscale noise image.
2. Image is divided into `block_px x block_px` blocks.
3. Each block average brightness is thresholded (`avg >= 142`) into one binary cloud cell.
4. Result is stored in `cell_map` and reused every frame.

This gives a procedural `clouds.png`-style repeating bitmap map.

### Update

`Clouds_Update()` only advances drift:

- `scroll_x += speed_x * dt`

No per-cell simulation or regeneration runs every frame.

### Rendering

`Clouds_Draw()`:

1. Converts camera position into cloud-cell coordinates.
2. Iterates a bounded square (`radius_cells`) around the camera.
3. Wraps world cell coordinates into `cell_map` indices (infinite tiling).
4. Draws one flat quad for every occupied cell using `DrawTriangle3D`.

Render state:

- Depth test and depth writes are disabled during cloud draw.
- Backface culling is disabled so tiles are visible from below.
- State is restored after cloud pass.

Coloring:

- Brightness tint is derived from ambient (`220 + 35 * ambient_multiplier`).
- Alpha comes from `cloud_opacity` (`cloud_opacity * 255`).

Cloud pass order is sky-first in `game_draw_world_pass()`:

- `Clouds_Draw(...)`
- `World_Draw(...)`

So terrain draws over clouds naturally.

### Tuning Knobs

Primary parameters in `Clouds_Init()` (`src/gfx/clouds.c`):

- `cell_size`: larger = chunkier cloud pieces.
- `radius_cells`: larger = farther cloud horizon, more draw cost.
- `layer_y`: cloud layer altitude.
- `speed_x`: drift speed and direction.
- `noise_size`: map repeat period (world tiling frequency).
- `block_px`: cell granularity from source image (higher = bigger blob features).
- `cloud_opacity`: overall transparency of clouds.
- threshold constant (`avg >= 142`): controls cloud coverage density.
