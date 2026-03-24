#include "world/mesher.h"
#include "world/world.h"
#include "world/blocks.h"
#include "world/chunk.h"
#include "gfx/shader_paths.h"

#include <stdint.h>
#include <string.h>
#include <voxel/mesher.h>
#include <raylib.h>
#include <stdlib.h>

#include "constants.h"

static Shader cutout_shader = {0};
static bool cutout_shader_loaded = false;

static Shader opaque_ao_shader = {0};
static bool opaque_ao_shader_loaded = false;

static Shader translucent_shader = {0};
static bool translucent_shader_loaded = false;

static int floor_div16(int x) {
  if (x >= 0) {
    return x >> 4;
  }
  return -(((-x) + 15) >> 4);
}

static Shader get_cutout_shader(void) {
  if (!cutout_shader_loaded) {
    char vs_path[256];
    char fs_path[256];
    ShaderPaths_Resolve(vs_path, sizeof(vs_path), "cutout.vs");
    ShaderPaths_Resolve(fs_path, sizeof(fs_path), "cutout.fs");
    cutout_shader = LoadShader(vs_path, fs_path);
    cutout_shader_loaded = true;
  }
  return cutout_shader;
}

static Shader get_opaque_ao_shader(void) {
  if (!opaque_ao_shader_loaded) {
    char vs_path[256];
    char fs_path[256];
    ShaderPaths_Resolve(vs_path, sizeof(vs_path), "opaque_ao.vs");
    ShaderPaths_Resolve(fs_path, sizeof(fs_path), "opaque_ao.fs");
    opaque_ao_shader = LoadShader(vs_path, fs_path);

    float aoMin = 0.60f;
    float aoCurve = 1.15f;

    int locMin = GetShaderLocation(opaque_ao_shader, "uAoMin");
    int locCurve = GetShaderLocation(opaque_ao_shader, "uAoCurve");

    if (locMin >= 0)
      SetShaderValue(opaque_ao_shader, locMin, &aoMin, SHADER_UNIFORM_FLOAT);
    if (locCurve >= 0)
      SetShaderValue(opaque_ao_shader, locCurve, &aoCurve, SHADER_UNIFORM_FLOAT);

    opaque_ao_shader_loaded = true;
  }

  return opaque_ao_shader;
}

static Shader get_translucent_shader(void) {
  if (!translucent_shader_loaded) {
    char vs_path[256];
    char fs_path[256];
    ShaderPaths_Resolve(vs_path, sizeof(vs_path), "translucent.vs");
    ShaderPaths_Resolve(fs_path, sizeof(fs_path), "translucent.fs");
    translucent_shader = LoadShader(vs_path, fs_path);
    translucent_shader_loaded = true;
  }
  return translucent_shader;
}

/* Callback implementations for libvoxel mesher */
static uint8_t cb_get_world_block(void *world_data, int wx, int y, int wz) {
  return World_GetBlock((World *)world_data, wx, y, wz);
}

static int cb_get_world_skylight(void *world_data, int wx, int y, int wz) {
  World *world = (World *)world_data;

  int cx = floor_div16(wx);
  int cz = floor_div16(wz);
  if (World_GetChunkConst(world, cx, cz) == NULL) {
    if (y < 0) {
      return 0;
    }
    if (y >= WORLD_MAX_HEIGHT) {
      return 15;
    }
    /* Avoid black boundary faces while neighbor chunks are not loaded yet. */
    return 15;
  }

  return World_GetSkyLight(world, wx, y, wz);
}

static void assign_model_to_chunk(World *world, VoxelMeshData *mesh_data, Chunk *chunk, bool cutout,
                                  bool translucent_solid, bool water) {
  /* Free existing model */
  void **model_ptr;
  bool *has_model;

  if (cutout) {
    model_ptr = &chunk->cutout_model;
    has_model = &chunk->has_cutout_model;
  } else if (translucent_solid) {
    model_ptr = &chunk->translucent_solid_model;
    has_model = &chunk->has_translucent_solid_model;
  } else if (water) {
    model_ptr = &chunk->translucent_model;
    has_model = &chunk->has_translucent_model;
  } else {
    model_ptr = &chunk->solid_model;
    has_model = &chunk->has_solid_model;
  }

  if (*has_model && *model_ptr) {
    UnloadModel(*(Model *)*model_ptr);
    free(*model_ptr);
    *model_ptr = NULL;
    *has_model = false;
  }

  if (mesh_data->vertex_count == 0) {
    return;
  }

  /* Copy stb_ds arrays to regular malloc'd arrays for raylib */
  int vert_size = mesh_data->vertex_count * 3 * sizeof(float);
  int tex_size = mesh_data->vertex_count * 2 * sizeof(float);
  int color_size = mesh_data->vertex_count * 4 * sizeof(uint8_t);

  float *vertices = (float *)malloc(vert_size);
  float *texcoords = (float *)malloc(tex_size);
  uint8_t *colors = (uint8_t *)malloc(color_size);
  if (vertices == NULL || texcoords == NULL || colors == NULL) {
    free(vertices);
    free(texcoords);
    free(colors);
    return;
  }

  memcpy(vertices, mesh_data->vertices, vert_size);
  memcpy(texcoords, mesh_data->texcoords, tex_size);
  memcpy(colors, mesh_data->colors, color_size);

  /* Create raylib mesh */
  Mesh mesh = {0};
  mesh.vertexCount = mesh_data->vertex_count;
  mesh.triangleCount = mesh_data->vertex_count / 3;
  mesh.vertices = vertices;
  mesh.texcoords = texcoords;
  mesh.colors = colors;

  UploadMesh(&mesh, false);

  Model model = LoadModelFromMesh(mesh);
  SetMaterialTexture(&model.materials[0], MATERIAL_MAP_DIFFUSE, world->terrain_texture);

  if (cutout) {
    model.materials[0].shader = get_cutout_shader();
  } else if (translucent_solid || water) {
    model.materials[0].shader = get_translucent_shader();
  } else {
    model.materials[0].shader = get_opaque_ao_shader();
  }

  Model *allocated_model = (Model *)malloc(sizeof(Model));
  if (allocated_model == NULL) {
    UnloadModel(model);
    return;
  }
  *allocated_model = model;
  *model_ptr = allocated_model;
  *has_model = true;
}

void Mesher_RebuildChunk(struct World *world, Chunk *chunk, float ambient_multiplier) {
  (void)ambient_multiplier;

  VoxelMesherConfig config = {
      .chunk_size_x = WORLD_CHUNK_SIZE_X,
      .chunk_size_y = WORLD_MAX_HEIGHT,
      .chunk_size_z = WORLD_CHUNK_SIZE_Z,
      .tile_size = 1.0f / 16.0f,
      .texel_padding = 0.5f,
      .light_lut = world->light_lut,
  };

  VoxelMesherCallbacks callbacks = {
      .get_world_block = cb_get_world_block,
      .get_world_skylight = cb_get_world_skylight,
      .world_data = world,
  };

  VoxelMeshData solid_mesh = {0};
  VoxelMeshData cutout_mesh = {0};
  VoxelMeshData translucent_solid_mesh = {0};
  VoxelMeshData water_mesh = {0};

  VoxelMesher_BuildChunk(chunk, &g_block_registry, &config, &callbacks, &solid_mesh, &cutout_mesh,
                         &translucent_solid_mesh, &water_mesh);

  assign_model_to_chunk(world, &solid_mesh, chunk, false, false, false);
  assign_model_to_chunk(world, &cutout_mesh, chunk, true, false, false);
  assign_model_to_chunk(world, &translucent_solid_mesh, chunk, false, true, false);
  assign_model_to_chunk(world, &water_mesh, chunk, false, false, true);

  VoxelMeshData_Free(&solid_mesh);
  VoxelMeshData_Free(&cutout_mesh);
  VoxelMeshData_Free(&translucent_solid_mesh);
  VoxelMeshData_Free(&water_mesh);
}

void Mesher_Shutdown(void) {
  if (cutout_shader_loaded && cutout_shader.id != 0) {
    UnloadShader(cutout_shader);
  }
  cutout_shader = (Shader){0};
  cutout_shader_loaded = false;

  if (opaque_ao_shader_loaded && opaque_ao_shader.id != 0) {
    UnloadShader(opaque_ao_shader);
  }
  opaque_ao_shader = (Shader){0};
  opaque_ao_shader_loaded = false;

  if (translucent_shader_loaded && translucent_shader.id != 0) {
    UnloadShader(translucent_shader);
  }
  translucent_shader = (Shader){0};
  translucent_shader_loaded = false;
}
