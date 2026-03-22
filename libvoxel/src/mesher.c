#include "voxel/mesher.h"
#include "voxel/chunk.h"
#include "voxel/block.h"

#include "stb_ds.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void VoxelMeshData_Init(VoxelMeshData *mesh) {
  mesh->vertices = NULL;
  mesh->texcoords = NULL;
  mesh->colors = NULL;
  mesh->vertex_count = 0;
  mesh->capacity = 0;
}

void VoxelMeshData_Free(VoxelMeshData *mesh) {
  if (mesh->vertices)
    arrfree(mesh->vertices);
  if (mesh->texcoords)
    arrfree(mesh->texcoords);
  if (mesh->colors)
    arrfree(mesh->colors);
  mesh->vertices = NULL;
  mesh->texcoords = NULL;
  mesh->colors = NULL;
  mesh->vertex_count = 0;
  mesh->capacity = 0;
}

void VoxelMeshData_Clear(VoxelMeshData *mesh) {
  arrsetlen(mesh->vertices, 0);
  arrsetlen(mesh->texcoords, 0);
  arrsetlen(mesh->colors, 0);
  mesh->vertex_count = 0;
}

static void mesh_push_vertex(VoxelMeshData *mesh, float x, float y, float z, float u, float v,
                             uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  arrput(mesh->vertices, x);
  arrput(mesh->vertices, y);
  arrput(mesh->vertices, z);

  arrput(mesh->texcoords, u);
  arrput(mesh->texcoords, v);

  arrput(mesh->colors, r);
  arrput(mesh->colors, g);
  arrput(mesh->colors, b);
  arrput(mesh->colors, a);

  mesh->vertex_count++;
  mesh->capacity = arrlen(mesh->vertices) / 3;
}

static void mesh_push_quad(VoxelMeshData *mesh, const float pos[4][3], const float uv[4][2],
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  mesh_push_vertex(mesh, pos[0][0], pos[0][1], pos[0][2], uv[0][0], uv[0][1], r, g, b, a);
  mesh_push_vertex(mesh, pos[1][0], pos[1][1], pos[1][2], uv[1][0], uv[1][1], r, g, b, a);
  mesh_push_vertex(mesh, pos[2][0], pos[2][1], pos[2][2], uv[2][0], uv[2][1], r, g, b, a);
  mesh_push_vertex(mesh, pos[0][0], pos[0][1], pos[0][2], uv[0][0], uv[0][1], r, g, b, a);
  mesh_push_vertex(mesh, pos[2][0], pos[2][1], pos[2][2], uv[2][0], uv[2][1], r, g, b, a);
  mesh_push_vertex(mesh, pos[3][0], pos[3][1], pos[3][2], uv[3][0], uv[3][1], r, g, b, a);
}

static void mesh_push_quad_colored(VoxelMeshData *mesh, const float pos[4][3], const float uv[4][2],
                                   const uint8_t color[4][4], bool flip) {
  static const int idx_normal[6] = {0, 1, 2, 0, 2, 3};
  static const int idx_flip[6] = {0, 1, 3, 1, 2, 3};
  const int *idx = flip ? idx_flip : idx_normal;

  for (int i = 0; i < 6; i++) {
    int v = idx[i];
    mesh_push_vertex(mesh, pos[v][0], pos[v][1], pos[v][2], uv[v][0], uv[v][1], color[v][0],
                     color[v][1], color[v][2], color[v][3]);
  }
}

static void mesh_push_double_sided_quad(VoxelMeshData *mesh, const float pos[4][3],
                                        const float uv[4][2], uint8_t r, uint8_t g, uint8_t b,
                                        uint8_t a) {
  mesh_push_quad(mesh, pos, uv, r, g, b, a);

  float back[4][3] = {
      {pos[3][0], pos[3][1], pos[3][2]},
      {pos[2][0], pos[2][1], pos[2][2]},
      {pos[1][0], pos[1][1], pos[1][2]},
      {pos[0][0], pos[0][1], pos[0][2]},
  };
  float back_uv[4][2] = {
      {uv[3][0], uv[3][1]},
      {uv[2][0], uv[2][1]},
      {uv[1][0], uv[1][1]},
      {uv[0][0], uv[0][1]},
  };
  mesh_push_quad(mesh, back, back_uv, r, g, b, a);
}

static const int NEIGHBOR_OFFSETS[6][3] = {
    [VOXEL_FACE_DOWN] = {0, -1, 0}, [VOXEL_FACE_UP] = {0, 1, 0},    [VOXEL_FACE_NORTH] = {0, 0, -1},
    [VOXEL_FACE_SOUTH] = {0, 0, 1}, [VOXEL_FACE_WEST] = {-1, 0, 0}, [VOXEL_FACE_EAST] = {1, 0, 0},
};

static void set_corner(float out[3], float x, float y, float z) {
  out[0] = x;
  out[1] = y;
  out[2] = z;
}

static void face_vertices(float x, float y, float z, int face, float out_pos[4][3], float height) {
  float x0 = x, x1 = x + 1.0f;
  float y0 = y, y1 = y + height;
  float z0 = z, z1 = z + 1.0f;

  switch (face) {
  case VOXEL_FACE_DOWN:
    set_corner(out_pos[0], x0, y0, z0);
    set_corner(out_pos[1], x1, y0, z0);
    set_corner(out_pos[2], x1, y0, z1);
    set_corner(out_pos[3], x0, y0, z1);
    break;
  case VOXEL_FACE_UP:
    set_corner(out_pos[0], x0, y1, z0);
    set_corner(out_pos[1], x0, y1, z1);
    set_corner(out_pos[2], x1, y1, z1);
    set_corner(out_pos[3], x1, y1, z0);
    break;
  case VOXEL_FACE_NORTH:
    set_corner(out_pos[0], x0, y0, z0);
    set_corner(out_pos[1], x0, y1, z0);
    set_corner(out_pos[2], x1, y1, z0);
    set_corner(out_pos[3], x1, y0, z0);
    break;
  case VOXEL_FACE_SOUTH:
    set_corner(out_pos[0], x0, y0, z1);
    set_corner(out_pos[1], x1, y0, z1);
    set_corner(out_pos[2], x1, y1, z1);
    set_corner(out_pos[3], x0, y1, z1);
    break;
  case VOXEL_FACE_WEST:
    set_corner(out_pos[0], x0, y0, z0);
    set_corner(out_pos[1], x0, y0, z1);
    set_corner(out_pos[2], x0, y1, z1);
    set_corner(out_pos[3], x0, y1, z0);
    break;
  case VOXEL_FACE_EAST:
  default:
    set_corner(out_pos[0], x1, y0, z0);
    set_corner(out_pos[1], x1, y1, z0);
    set_corner(out_pos[2], x1, y1, z1);
    set_corner(out_pos[3], x1, y0, z1);
    break;
  }
}

static void set_uv(float out[2], float u, float v) {
  out[0] = u;
  out[1] = v;
}

static void face_uvs(int face, float u0, float v0, float u1, float v1, float out_uv[4][2]) {
  switch (face) {
  case VOXEL_FACE_DOWN:
    set_uv(out_uv[0], u0, v1);
    set_uv(out_uv[1], u1, v1);
    set_uv(out_uv[2], u1, v0);
    set_uv(out_uv[3], u0, v0);
    break;
  case VOXEL_FACE_UP:
    set_uv(out_uv[0], u0, v1);
    set_uv(out_uv[1], u0, v0);
    set_uv(out_uv[2], u1, v0);
    set_uv(out_uv[3], u1, v1);
    break;
  case VOXEL_FACE_SOUTH:
  case VOXEL_FACE_WEST:
    set_uv(out_uv[0], u1, v1);
    set_uv(out_uv[1], u0, v1);
    set_uv(out_uv[2], u0, v0);
    set_uv(out_uv[3], u1, v0);
    break;
  case VOXEL_FACE_NORTH:
  case VOXEL_FACE_EAST:
  default:
    set_uv(out_uv[0], u0, v1);
    set_uv(out_uv[1], u0, v0);
    set_uv(out_uv[2], u1, v0);
    set_uv(out_uv[3], u1, v1);
    break;
  }
}

/* Face brightness multipliers for directional shading */
static const float FACE_BRIGHTNESS[6] = {
    [VOXEL_FACE_DOWN] = 0.55f,  [VOXEL_FACE_UP] = 1.00f,   [VOXEL_FACE_NORTH] = 0.82f,
    [VOXEL_FACE_SOUTH] = 0.82f, [VOXEL_FACE_WEST] = 0.70f, [VOXEL_FACE_EAST] = 0.70f,
};

static inline int clamp_light(int light) { return (light < 0) ? 0 : ((light > 15) ? 15 : light); }

static inline float clamp_luminance(float luminance) {
  return (luminance < 0.0f) ? 0.0f : ((luminance > 1.0f) ? 1.0f : luminance);
}

static float compute_cutout_luminance(const VoxelMesherCallbacks *cb, int wx, int y, int wz,
                                      const VoxelMesherConfig *config) {
  /*
   * Cross-plant blocks (like tall grass) should not be lit only from +Y.
   * Sample local and horizontal neighbors so a single overhead block does not
   * force the entire plant to black when side skylight is available.
   */
  static const int SAMPLE_OFFSETS[5][3] = {
      {0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1},
  };

  int max_light = 0;
  for (int i = 0; i < 5; i++) {
    int light = cb->get_world_skylight(cb->world_data, wx + SAMPLE_OFFSETS[i][0], y,
                                       wz + SAMPLE_OFFSETS[i][2]);
    light = clamp_light(light);
    if (light > max_light) {
      max_light = light;
    }
  }

  return clamp_luminance(config->light_lut[max_light]);
}

static float compute_face_luminance(const VoxelMesherCallbacks *cb, int wx, int y, int wz, int face,
                                    const VoxelMesherConfig *config) {
  /* Sample skylight from the neighbor block in the face direction */
  int nx = wx + NEIGHBOR_OFFSETS[face][0];
  int ny = y + NEIGHBOR_OFFSETS[face][1];
  int nz = wz + NEIGHBOR_OFFSETS[face][2];

  int light = cb->get_world_skylight(cb->world_data, nx, ny, nz);
  light = clamp_light(light);

  /* Apply light LUT and directional brightness */
  float luminance = config->light_lut[light] * FACE_BRIGHTNESS[face];
  return clamp_luminance(luminance);
}

static void face_color(const VoxelBlockRegistry *registry, const VoxelMesherCallbacks *cb,
                       const VoxelMesherConfig *config, uint8_t block_id, int wx, int y, int wz,
                       int face, uint8_t *r_out, uint8_t *g_out, uint8_t *b_out) {
  /* Compute lighting for this face */
  float luminance = compute_face_luminance(cb, wx, y, wz, face, config);

  /* Get block tint color */
  uint8_t tr = 255, tg = 255, tb = 255;
  VoxelBlock_GetTint(registry, block_id, face, &tr, &tg, &tb);

  /* Multiply tint by luminance */

  /* Multiply tint by luminance */
  *r_out = (uint8_t)fminf(255.0f, luminance * (float)tr);
  *g_out = (uint8_t)fminf(255.0f, luminance * (float)tg);
  *b_out = (uint8_t)fminf(255.0f, luminance * (float)tb);
}

static void cutout_color(const VoxelBlockRegistry *registry, const VoxelMesherCallbacks *cb,
                         const VoxelMesherConfig *config, uint8_t block_id, int wx, int y, int wz,
                         uint8_t *r_out, uint8_t *g_out, uint8_t *b_out) {
  float luminance = compute_cutout_luminance(cb, wx, y, wz, config);

  uint8_t tr = 255, tg = 255, tb = 255;
  VoxelBlock_GetTint(registry, block_id, VOXEL_FACE_UP, &tr, &tg, &tb);

  *r_out = (uint8_t)fminf(255.0f, luminance * (float)tr);
  *g_out = (uint8_t)fminf(255.0f, luminance * (float)tg);
  *b_out = (uint8_t)fminf(255.0f, luminance * (float)tb);
}

/* ============================================================================
 * Ambient Occlusion
 * ============================================================================ */

static inline bool ao_occludes(const VoxelBlockRegistry *registry, uint8_t block_id) {
  return VoxelBlock_IsOpaque(registry, block_id);
}

static inline int vertex_ao_level(bool side1, bool side2, bool corner) {
  /* If both sides are occluded, maximum darkening */
  if (side1 && side2)
    return 0;

  /* Otherwise, count occluded neighbors (0-3 scale) */
  return 3 - ((int)side1 + (int)side2 + (int)corner);
}

static int sample_vertex_ao(const VoxelBlockRegistry *registry, const VoxelMesherCallbacks *cb,
                            int x1, int y1, int z1, /* side 1 */
                            int x2, int y2, int z2, /* side 2 */
                            int xc, int yc, int zc) /* corner */ {
  bool s1 = ao_occludes(registry, cb->get_world_block(cb->world_data, x1, y1, z1));
  bool s2 = ao_occludes(registry, cb->get_world_block(cb->world_data, x2, y2, z2));
  bool c = ao_occludes(registry, cb->get_world_block(cb->world_data, xc, yc, zc));
  return vertex_ao_level(s1, s2, c);
}

static void face_corner_ao(const VoxelBlockRegistry *registry, const VoxelMesherCallbacks *cb,
                           int wx, int y, int wz, int face, int out_ao[4]) {
  /*
   * Calculate AO for each corner of a face by sampling 3 neighbors:
   * - Two side blocks adjacent to the corner
   * - One diagonal corner block
   *
   * Vertex order matches face_vertices() output
   */
  switch (face) {
  case VOXEL_FACE_UP:
    out_ao[0] =
        sample_vertex_ao(registry, cb, wx - 1, y + 1, wz, wx, y + 1, wz - 1, wx - 1, y + 1, wz - 1);
    out_ao[1] =
        sample_vertex_ao(registry, cb, wx - 1, y + 1, wz, wx, y + 1, wz + 1, wx - 1, y + 1, wz + 1);
    out_ao[2] =
        sample_vertex_ao(registry, cb, wx + 1, y + 1, wz, wx, y + 1, wz + 1, wx + 1, y + 1, wz + 1);
    out_ao[3] =
        sample_vertex_ao(registry, cb, wx + 1, y + 1, wz, wx, y + 1, wz - 1, wx + 1, y + 1, wz - 1);
    break;

  case VOXEL_FACE_DOWN:
    out_ao[0] =
        sample_vertex_ao(registry, cb, wx - 1, y - 1, wz, wx, y - 1, wz - 1, wx - 1, y - 1, wz - 1);
    out_ao[1] =
        sample_vertex_ao(registry, cb, wx + 1, y - 1, wz, wx, y - 1, wz - 1, wx + 1, y - 1, wz - 1);
    out_ao[2] =
        sample_vertex_ao(registry, cb, wx + 1, y - 1, wz, wx, y - 1, wz + 1, wx + 1, y - 1, wz + 1);
    out_ao[3] =
        sample_vertex_ao(registry, cb, wx - 1, y - 1, wz, wx, y - 1, wz + 1, wx - 1, y - 1, wz + 1);
    break;

  case VOXEL_FACE_NORTH:
    out_ao[0] =
        sample_vertex_ao(registry, cb, wx - 1, y, wz - 1, wx, y - 1, wz - 1, wx - 1, y - 1, wz - 1);
    out_ao[1] =
        sample_vertex_ao(registry, cb, wx - 1, y, wz - 1, wx, y + 1, wz - 1, wx - 1, y + 1, wz - 1);
    out_ao[2] =
        sample_vertex_ao(registry, cb, wx + 1, y, wz - 1, wx, y + 1, wz - 1, wx + 1, y + 1, wz - 1);
    out_ao[3] =
        sample_vertex_ao(registry, cb, wx + 1, y, wz - 1, wx, y - 1, wz - 1, wx + 1, y - 1, wz - 1);
    break;

  case VOXEL_FACE_SOUTH:
    out_ao[0] =
        sample_vertex_ao(registry, cb, wx - 1, y, wz + 1, wx, y - 1, wz + 1, wx - 1, y - 1, wz + 1);
    out_ao[1] =
        sample_vertex_ao(registry, cb, wx + 1, y, wz + 1, wx, y - 1, wz + 1, wx + 1, y - 1, wz + 1);
    out_ao[2] =
        sample_vertex_ao(registry, cb, wx + 1, y, wz + 1, wx, y + 1, wz + 1, wx + 1, y + 1, wz + 1);
    out_ao[3] =
        sample_vertex_ao(registry, cb, wx - 1, y, wz + 1, wx, y + 1, wz + 1, wx - 1, y + 1, wz + 1);
    break;

  case VOXEL_FACE_WEST:
    out_ao[0] =
        sample_vertex_ao(registry, cb, wx - 1, y - 1, wz, wx - 1, y, wz - 1, wx - 1, y - 1, wz - 1);
    out_ao[1] =
        sample_vertex_ao(registry, cb, wx - 1, y - 1, wz, wx - 1, y, wz + 1, wx - 1, y - 1, wz + 1);
    out_ao[2] =
        sample_vertex_ao(registry, cb, wx - 1, y + 1, wz, wx - 1, y, wz + 1, wx - 1, y + 1, wz + 1);
    out_ao[3] =
        sample_vertex_ao(registry, cb, wx - 1, y + 1, wz, wx - 1, y, wz - 1, wx - 1, y + 1, wz - 1);
    break;

  case VOXEL_FACE_EAST:
  default:
    out_ao[0] =
        sample_vertex_ao(registry, cb, wx + 1, y - 1, wz, wx + 1, y, wz - 1, wx + 1, y - 1, wz - 1);
    out_ao[1] =
        sample_vertex_ao(registry, cb, wx + 1, y + 1, wz, wx + 1, y, wz - 1, wx + 1, y + 1, wz - 1);
    out_ao[2] =
        sample_vertex_ao(registry, cb, wx + 1, y + 1, wz, wx + 1, y, wz + 1, wx + 1, y + 1, wz + 1);
    out_ao[3] =
        sample_vertex_ao(registry, cb, wx + 1, y - 1, wz, wx + 1, y, wz + 1, wx + 1, y - 1, wz + 1);
    break;
  }
}

static void append_cross_plant(VoxelMeshData *mesh, const VoxelMesherConfig *config, int tile,
                               float x, float y, float z, uint8_t r, uint8_t g, uint8_t b,
                               uint8_t a) {
  float tu = (float)(tile % 16) * config->tile_size;
  float tv = (float)(tile / 16) * config->tile_size;
  float texel = config->texel_padding / 256.0f;
  float u0 = tu + texel * 0.1f;
  float v0 = tv + texel * 0.1f;
  float u1 = tu + config->tile_size - texel * 0.1f;
  float v1 = tv + config->tile_size - texel * 0.1f;

  float uv[4][2] = {{u1, v1}, {u0, v1}, {u0, v0}, {u1, v0}};

  const float inset = 0.16f;
  const float min = inset;
  const float max = 1.0f - inset;
  const float top = 0.9f;

  float quad0[4][3] = {
      {x + min, y, z + min},
      {x + max, y, z + max},
      {x + max, y + top, z + max},
      {x + min, y + top, z + min},
  };

  float quad1[4][3] = {
      {x + min, y, z + max},
      {x + max, y, z + min},
      {x + max, y + top, z + min},
      {x + min, y + top, z + max},
  };

  mesh_push_double_sided_quad(mesh, quad0, uv, r, g, b, a);
  mesh_push_double_sided_quad(mesh, quad1, uv, r, g, b, a);
}

void VoxelMesher_BuildChunk(const VoxelChunk *chunk, const VoxelBlockRegistry *registry,
                            const VoxelMesherConfig *config, const VoxelMesherCallbacks *cb,
                            VoxelMeshData *solid, VoxelMeshData *translucent,
                            VoxelMeshData *cutout) {
  VoxelMeshData_Clear(solid);
  VoxelMeshData_Clear(translucent);
  VoxelMeshData_Clear(cutout);

  /* Water surface height constant */
  const float EXPOSED_WATER_HEIGHT = 0.90f;

  for (int lx = 0; lx < config->chunk_size_x; lx++) {
    for (int lz = 0; lz < config->chunk_size_z; lz++) {
      for (int y = 0; y < config->chunk_size_y; y++) {
        uint8_t block_id = VoxelChunk_GetBlock(chunk, lx, y, lz);
        if (block_id == 0) { /* AIR */
          continue;
        }

        int wx = chunk->cx * config->chunk_size_x + lx;
        int wz = chunk->cz * config->chunk_size_z + lz;

        /* Determine block height (for water surface) */
        float block_height = 1.0f;
        bool is_water =
            VoxelBlock_IsTranslucent(registry, block_id) && (block_id == 9); /* BLOCK_WATER */

        if (is_water) {
          /* Check if water is exposed to air above */
          uint8_t above_id = cb->get_world_block(cb->world_data, wx, y + 1, wz);
          if (above_id == 0) { /* AIR above */
            block_height = EXPOSED_WATER_HEIGHT;
          }
        }

        /* Check if this is a plant block (needs special cross-plant rendering) */
        bool is_plant = VoxelBlock_IsTranslucent(registry, block_id) &&
                        VoxelBlock_Opacity(registry, block_id) <= 1;

        if (is_plant) {
          /* Render as cross-plant for tall grass (opacity 0) */
          if (VoxelBlock_Opacity(registry, block_id) == 0) {
            uint8_t r, g, b;
            cutout_color(registry, cb, config, block_id, wx, y, wz, &r, &g, &b);
            int tile = VoxelBlock_Texture(registry, block_id, VOXEL_FACE_UP);
            append_cross_plant(cutout, config, tile, (float)lx, (float)y, (float)lz, r, g, b, 255);
            continue;
          }

          /* Render leaves with all faces - no culling for same block type */
          for (int face = 0; face < 6; face++) {
            int nx = wx + NEIGHBOR_OFFSETS[face][0];
            int ny = y + NEIGHBOR_OFFSETS[face][1];
            int nz = wz + NEIGHBOR_OFFSETS[face][2];

            uint8_t neighbor_id = cb->get_world_block(cb->world_data, nx, ny, nz);

            /* Only cull against opaque blocks, not against other leaves */
            if (VoxelBlock_IsOpaque(registry, neighbor_id)) {
              continue;
            }

            int tile = VoxelBlock_Texture(registry, block_id, face);
            float tu = (float)(tile % 16) * config->tile_size;
            float tv = (float)(tile / 16) * config->tile_size;
            float texel = config->texel_padding / 256.0f;
            float u0 = tu + texel * 0.5f;
            float v0 = tv + texel * 0.5f;
            float u1 = tu + config->tile_size - texel * 0.5f;
            float v1 = tv + config->tile_size - texel * 0.5f;

            float face_pos[4][3];
            face_vertices((float)lx, (float)y, (float)lz, face, face_pos, 1.0f);

            float uv[4][2];
            face_uvs(face, u0, v0, u1, v1, uv);

            uint8_t r, g, b;
            face_color(registry, cb, config, block_id, wx, y, wz, face, &r, &g, &b);
            mesh_push_quad(cutout, face_pos, uv, r, g, b, 255);
          }
          continue;
        }

        bool is_translucent_block = VoxelBlock_IsTranslucent(registry, block_id);
        VoxelMeshData *target_mesh = is_translucent_block ? translucent : solid;
        uint8_t alpha = is_translucent_block ? 170 : 255;

        for (int face = 0; face < 6; face++) {
          int nx = wx + NEIGHBOR_OFFSETS[face][0];
          int ny = y + NEIGHBOR_OFFSETS[face][1];
          int nz = wz + NEIGHBOR_OFFSETS[face][2];

          uint8_t neighbor_id = cb->get_world_block(cb->world_data, nx, ny, nz);

          /* Face culling logic */
          bool visible = true;

          /* Water only renders faces when touching air */
          if (is_water) {
            visible = (neighbor_id == 0); /* Only show face if neighbor is AIR */
          } else if (neighbor_id == block_id) {
            visible = false;
          } else if (VoxelBlock_IsOpaque(registry, neighbor_id)) {
            visible = false;
          }

          if (!visible) {
            continue;
          }

          int tile = VoxelBlock_Texture(registry, block_id, face);
          float tu = (float)(tile % 16) * config->tile_size;
          float tv = (float)(tile / 16) * config->tile_size;
          float texel = config->texel_padding / 256.0f;
          float u0 = tu + texel * 0.5f;
          float v0 = tv + texel * 0.5f;
          float u1 = tu + config->tile_size - texel * 0.5f;
          float v1 = tv + config->tile_size - texel * 0.5f;

          float face_pos[4][3];
          face_vertices((float)lx, (float)y, (float)lz, face, face_pos, block_height);

          float uv[4][2];
          face_uvs(face, u0, v0, u1, v1, uv);

          uint8_t r, g, b;
          face_color(registry, cb, config, block_id, wx, y, wz, face, &r, &g, &b);

          if (!is_translucent_block) {
            /* Solid blocks: calculate per-corner AO */
            int ao[4];
            face_corner_ao(registry, cb, wx, y, wz, face, ao);

            /* Pack RGB lighting and AO alpha into per-vertex colors */
            uint8_t colors[4][4];
            for (int i = 0; i < 4; i++) {
              colors[i][0] = r;
              colors[i][1] = g;
              colors[i][2] = b;
              colors[i][3] = (uint8_t)((ao[i] * 255) / 3); /* AO: 0-3 -> 0-255 */
            }

            /* Flip diagonal to avoid lighting seam artifacts */
            bool flip = (ao[0] + ao[2]) > (ao[1] + ao[3]);
            mesh_push_quad_colored(target_mesh, face_pos, uv, colors, flip);
          } else {
            /* Translucent blocks: use single color (no AO) */
            mesh_push_quad(target_mesh, face_pos, uv, r, g, b, alpha);
          }
        }
      }
    }
  }
}
