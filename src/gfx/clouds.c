#include "gfx/clouds.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#include "raylib.h"
#include "rlgl.h"

static void clouds_draw_quad(float cx, float y, float cz, float size, Color color) {
  float half = size * 0.5f;
  Vector3 p0 = {cx - half, y, cz - half};
  Vector3 p1 = {cx + half, y, cz - half};
  Vector3 p2 = {cx + half, y, cz + half};
  Vector3 p3 = {cx - half, y, cz + half};

  DrawTriangle3D(p0, p1, p2, color);
  DrawTriangle3D(p0, p2, p3, color);
}

static int wrapi(int v, int m) {
  if (m <= 0) {
    return 0;
  }

  v %= m;
  if (v < 0) {
    v += m;
  }
  return v;
}

static unsigned char *clouds_generate_cell_map(int noise_size, int block_px, int *out_grid_size) {
  if (noise_size <= 0 || block_px <= 0 || out_grid_size == NULL) {
    return NULL;
  }

  /* Build a deterministic cloud image source map once at startup. */
  Image perlin = GenImagePerlinNoise(noise_size, noise_size, 0, 0, 10.0f);
  Color *pixels = LoadImageColors(perlin);
  UnloadImage(perlin);
  if (pixels == NULL) {
    return NULL;
  }

  int grid = noise_size / block_px;
  if (grid <= 0) {
    UnloadImageColors(pixels);
    return NULL;
  }

  unsigned char *cell_map = (unsigned char *)calloc((size_t)grid * (size_t)grid, 1);
  if (cell_map == NULL) {
    UnloadImageColors(pixels);
    return NULL;
  }

  /*
   * Convert the grayscale image into cloud cells:
   * - each block_px x block_px image block becomes one cloud cell
   * - average brightness above threshold -> solid cloud cell (1)
   * - otherwise -> empty sky cell (0)
   */
  for (int gy = 0; gy < grid; gy++) {
    for (int gx = 0; gx < grid; gx++) {
      int sum = 0;
      for (int py = 0; py < block_px; py++) {
        for (int px = 0; px < block_px; px++) {
          int x = gx * block_px + px;
          int y = gy * block_px + py;
          Color c = pixels[y * noise_size + x];
          sum += (c.r + c.g + c.b) / 3;
        }
      }

      int sample_count = block_px * block_px;
      int avg = sum / sample_count;
      cell_map[gy * grid + gx] = (avg >= 142) ? 1 : 0;
    }
  }

  UnloadImageColors(pixels);
  *out_grid_size = grid;
  return cell_map;
}

void Clouds_Init(Clouds *clouds) {
  if (clouds == NULL) {
    return;
  }

  *clouds = *clouds;

  clouds->cell_map =
      clouds_generate_cell_map(clouds->noise_size, clouds->block_px, &clouds->grid_size);
}

void Clouds_Shutdown(Clouds *clouds) {
  if (clouds == NULL) {
    return;
  }

  if (clouds->cell_map != NULL) {
    free(clouds->cell_map);
  }
  *clouds = (Clouds){0};
}

void Clouds_Update(Clouds *clouds, float dt) {
  if (clouds == NULL || dt <= 0.0f) {
    return;
  }

  clouds->scroll_x += clouds->speed_x * dt;
}

void Clouds_Draw(Clouds *clouds, const Camera3D *camera, float ambient_multiplier) {
  if (clouds == NULL || camera == NULL || clouds->cell_map == NULL || clouds->grid_size <= 0) {
    return;
  }

  if (clouds->cell_size <= 0.0f || clouds->radius_cells <= 0) {
    return;
  }

  float scrolled_cam_x = camera->position.x + clouds->scroll_x;
  int base_cx = (int)floorf(scrolled_cam_x / clouds->cell_size);
  int base_cz = (int)floorf(camera->position.z / clouds->cell_size);

  /* Fast-cloud look: mostly opaque and flatter than world lighting. */
  unsigned char tint = (unsigned char)(220.0f + 35.0f * ambient_multiplier);
  Color cloud_color = {tint, tint, tint, clouds->cloud_opacity * 255.f};

  /* Render sky layer first: no depth write/test so terrain can draw over it later. */
  rlDisableBackfaceCulling();
  rlDisableDepthTest();
  rlDisableDepthMask();

  /*
   * Draw only cells around the camera in a bounded radius.
   * World cell coordinates are wrapped into cell_map coordinates, so the
   * finite map repeats infinitely while still drifting over time.
   */
  for (int dz = -clouds->radius_cells; dz <= clouds->radius_cells; dz++) {
    for (int dx = -clouds->radius_cells; dx <= clouds->radius_cells; dx++) {
      int wx_cell = base_cx + dx;
      int wz_cell = base_cz + dz;

      int mx = wrapi(wx_cell, clouds->grid_size);
      int mz = wrapi(wz_cell, clouds->grid_size);
      if (clouds->cell_map[mz * clouds->grid_size + mx] == 0) {
        continue;
      }

      /* Convert cell-space back to world-space and draw one flat cloud tile. */
      float world_x = ((float)wx_cell + 0.5f) * clouds->cell_size - clouds->scroll_x;
      float world_z = ((float)wz_cell + 0.5f) * clouds->cell_size;
      clouds_draw_quad(world_x, clouds->layer_y, world_z, clouds->cell_size, cloud_color);
    }
  }

  rlEnableDepthMask();
  rlEnableDepthTest();
  rlEnableBackfaceCulling();
}
