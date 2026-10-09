#include "world/mesher.h"
#include "voxel/chunk.h"
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

typedef struct MesherShader {
  const char *vs_name;
  const char *fs_name;
  Shader shader;
  bool loaded;
} MesherShader;

static MesherShader cutout_shader = {.vs_name = "cutout.vs", .fs_name = "cutout.fs"};
static MesherShader opaque_ao_shader = {.vs_name = "opaque_ao.vs", .fs_name = "opaque_ao.fs"};
static MesherShader translucent_shader = {.vs_name = "translucent.vs", .fs_name = "translucent.fs"};

static bool geometry_pixel_snap_enabled = false;
static Vector2 geometry_pixel_snap_resolution = {480.0f, 270.0f};

static void mesher_apply_geometry_snap_uniforms(Shader shader) {
  if (shader.id == 0) {
    return;
  }

  float enabled = geometry_pixel_snap_enabled ? 1.0f : 0.0f;
  int enabled_loc = GetShaderLocation(shader, "uGeometrySnapEnabled");
  int resolution_loc = GetShaderLocation(shader, "uGeometrySnapResolution");

  if (enabled_loc >= 0) {
    SetShaderValue(shader, enabled_loc, &enabled, SHADER_UNIFORM_FLOAT);
  }
  if (resolution_loc >= 0) {
    SetShaderValue(shader, resolution_loc, &geometry_pixel_snap_resolution, SHADER_UNIFORM_VEC2);
  }
}

static MesherShader *const all_shaders[] = {&cutout_shader, &opaque_ao_shader, &translucent_shader};

static void load_shader(MesherShader *entry) {
  char vs_path[256];
  char fs_path[256];
  ShaderPaths_Resolve(vs_path, sizeof(vs_path), entry->vs_name);
  ShaderPaths_Resolve(fs_path, sizeof(fs_path), entry->fs_name);
  entry->shader = LoadShader(vs_path, fs_path);
  entry->loaded = true;
}

static Shader get_shader(MesherShader *entry) {
  if (!entry->loaded) {
    load_shader(entry);
    mesher_apply_geometry_snap_uniforms(entry->shader);
  }
  return entry->shader;
}

static Shader get_opaque_ao_shader(void) {
  if (!opaque_ao_shader.loaded) {
    load_shader(&opaque_ao_shader);

    float aoMin = 0.60f;
    float aoCurve = 1.15f;

    int locMin = GetShaderLocation(opaque_ao_shader.shader, "uAoMin");
    int locCurve = GetShaderLocation(opaque_ao_shader.shader, "uAoCurve");

    if (locMin >= 0)
      SetShaderValue(opaque_ao_shader.shader, locMin, &aoMin, SHADER_UNIFORM_FLOAT);
    if (locCurve >= 0)
      SetShaderValue(opaque_ao_shader.shader, locCurve, &aoCurve, SHADER_UNIFORM_FLOAT);

    mesher_apply_geometry_snap_uniforms(opaque_ao_shader.shader);
  }

  return opaque_ao_shader.shader;
}

static void refresh_loaded_shaders(void) {
  for (size_t i = 0; i < sizeof(all_shaders) / sizeof(all_shaders[0]); i++) {
    if (all_shaders[i]->loaded) {
      mesher_apply_geometry_snap_uniforms(all_shaders[i]->shader);
    }
  }
}

void Mesher_SetGeometryPixelSnapEnabled(bool enabled) {
  geometry_pixel_snap_enabled = enabled;
  refresh_loaded_shaders();
}

void Mesher_SetGeometryPixelSnapResolution(float width, float height) {
  if (width < 1.0f || height < 1.0f) {
    return;
  }
  geometry_pixel_snap_resolution = (Vector2){width, height};
  refresh_loaded_shaders();
}

/* Callback implementations for libvoxel mesher */
static uint8_t cb_get_world_block(void *world_data, int wx, int y, int wz) {
  return World_GetBlock((World *)world_data, wx, y, wz);
}

static int cb_get_world_skylight(void *world_data, int wx, int y, int wz) {
  World *world = (World *)world_data;

  if (World_GetChunkConst(world, floor_div16(wx), floor_div16(wz)) == NULL) {
    /* Avoid black boundary faces while neighbor chunks are not loaded yet. */
    return (y < 0) ? 0 : 15;
  }

  return World_GetSkyLight(world, wx, y, wz);
}

static void assign_model_to_chunk(World *world, VoxelMeshData *mesh_data, Chunk *chunk,
                                  uint8_t pass, Shader shader) {
  void **model_slot = Chunk_ModelSlot(chunk, pass);

  if (VoxelChunk_HasRenderPass(chunk, pass) && *model_slot) {
    UnloadModel(*(Model *)*model_slot);
    free(*model_slot);
    *model_slot = NULL;
  }
  VoxelChunk_DisableRenderPass(chunk, pass);

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

  Mesh mesh = {0};
  mesh.vertexCount = mesh_data->vertex_count;
  mesh.triangleCount = mesh_data->vertex_count / 3;
  mesh.vertices = vertices;
  mesh.texcoords = texcoords;
  mesh.colors = colors;

  UploadMesh(&mesh, false);

  Model model = LoadModelFromMesh(mesh);
  SetMaterialTexture(&model.materials[0], MATERIAL_MAP_DIFFUSE, world->terrain_texture);
  model.materials[0].shader = shader;

  Model *allocated_model = (Model *)malloc(sizeof(Model));
  if (allocated_model == NULL) {
    UnloadModel(model);
    return;
  }
  *allocated_model = model;
  *model_slot = allocated_model;
  VoxelChunk_EnableRenderPass(chunk, pass);
}

void Mesher_RebuildChunk(struct World *world, Chunk *chunk) {
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

  assign_model_to_chunk(world, &solid_mesh, chunk, SOLID, get_opaque_ao_shader());
  assign_model_to_chunk(world, &cutout_mesh, chunk, CUTOUT, get_shader(&cutout_shader));
  assign_model_to_chunk(world, &translucent_solid_mesh, chunk, TRANSLUCENT_SOLID,
                        get_shader(&translucent_shader));
  assign_model_to_chunk(world, &water_mesh, chunk, TRANSLUCENT, get_shader(&translucent_shader));

  VoxelMeshData_Free(&solid_mesh);
  VoxelMeshData_Free(&cutout_mesh);
  VoxelMeshData_Free(&translucent_solid_mesh);
  VoxelMeshData_Free(&water_mesh);
}

void Mesher_Shutdown(void) {
  for (size_t i = 0; i < sizeof(all_shaders) / sizeof(all_shaders[0]); i++) {
    if (all_shaders[i]->loaded && all_shaders[i]->shader.id != 0) {
      UnloadShader(all_shaders[i]->shader);
    }
    all_shaders[i]->shader = (Shader){0};
    all_shaders[i]->loaded = false;
  }
}
