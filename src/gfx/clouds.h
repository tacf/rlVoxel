#ifndef RLVOXEL_CLOUDS_H
#define RLVOXEL_CLOUDS_H

#include <raylib.h>

typedef struct Clouds {
  float scroll_x;
  float speed_x;
  float cell_size;
  float layer_y;
  int radius_cells;
  int noise_size;
  int block_px;
  int grid_size;
  float cloud_opacity;
  unsigned char *cell_map;
} Clouds;

void Clouds_Init(Clouds *clouds);
void Clouds_Shutdown(Clouds *clouds);
void Clouds_Update(Clouds *clouds, float dt);
void Clouds_Draw(Clouds *clouds, const Camera3D *camera, float ambient_multiplier);

#endif /* RLVOXEL_CLOUDS_H */
