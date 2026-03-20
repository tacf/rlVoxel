#define STB_DS_IMPLEMENTATION
#include "world/world.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "raylib.h"
#include <raymath.h>
#include "rlgl.h"
#include "world/blocks.h"
#include "world/chunk.h"
#include <voxel/chunkmap.h>
#include "world/lighting.h"
#include "world/mesher.h"
#include "world/worldgen.h"
#include "profiling/profiler.h"

#ifndef WORLD_DEBUG_LOGS
#define WORLD_DEBUG_LOGS 0
#endif

#if WORLD_DEBUG_LOGS
#define WORLD_LOG(...) TraceLog(LOG_INFO, __VA_ARGS__)
#else
#define WORLD_LOG(...) ((void)0)
#endif

typedef struct ChunkSortEntry ChunkSortEntry;
static ChunkSortEntry *g_solid_draw_entries = NULL;
static ChunkSortEntry *g_translucent_draw_entries = NULL;
static ChunkSortEntry *g_cutout_draw_entries = NULL;

static inline int64_t make_chunk_key(int cx, int cz) { return ((int64_t)cx << 32) | (uint32_t)cz; }

static int floor_div16(int x) {
  if (x >= 0) {
    return x >> 4;
  }
  return -(((-x) + 15) >> 4);
}

static int floor_mod16(int x) {
  int m = x & 15;
  if (m < 0) {
    m += 16;
  }
  return m;
}

static LightUpdateQueue *world_queue(World *world) {
  return (LightUpdateQueue *)world->light_queue_ptr;
}

static const LightUpdateQueue *world_queue_const(const World *world) {
  return (const LightUpdateQueue *)world->light_queue_ptr;
}

static void world_enqueue_dirty_chunk_key(World *world, int64_t key) {
  if (world == NULL) {
    return;
  }
  if (hmgeti(world->dirty_chunk_key_set, key) >= 0) {
    return;
  }
  hmput(world->dirty_chunk_key_set, key, 1);
  arrput(world->dirty_chunk_keys, key);
}

static void world_enqueue_dirty_chunk(World *world, int cx, int cz) {
  world_enqueue_dirty_chunk_key(world, make_chunk_key(cx, cz));
}

static void world_compact_dirty_chunk_queue(World *world) {
  if (world == NULL) {
    return;
  }

  ptrdiff_t len = arrlen(world->dirty_chunk_keys);
  if (world->dirty_chunk_read_index <= 0) {
    return;
  }

  if (world->dirty_chunk_read_index >= len) {
    arrsetlen(world->dirty_chunk_keys, 0);
    world->dirty_chunk_read_index = 0;
    return;
  }

  if (world->dirty_chunk_read_index > 1024 && world->dirty_chunk_read_index * 2 >= len) {
    arrdeln(world->dirty_chunk_keys, 0, world->dirty_chunk_read_index);
    world->dirty_chunk_read_index = 0;
  }
}

Chunk *World_GetChunk(World *world, int cx, int cz) {
  int64_t key = make_chunk_key(cx, cz);
  return voxel_chunkmap_get(world->chunks, key);
}

const Chunk *World_GetChunkConst(const World *world, int cx, int cz) {
  int64_t key = make_chunk_key(cx, cz);
  return voxel_chunkmap_get(world->chunks, key);
}

Chunk *World_GetOrCreateChunk(World *world, int cx, int cz) {
  Chunk *existing = World_GetChunk(world, cx, cz);
  if (existing != NULL) {
    return existing;
  }

  Chunk *chunk = (Chunk *)calloc(1, sizeof(Chunk));
  if (!chunk)
    return NULL;

  Chunk_Init(chunk, cx, cz);
  WorldGen_GenerateChunk(&world->generator, chunk);
  Lighting_InitializeChunkSkylight(world, chunk);

  int64_t key = make_chunk_key(cx, cz);
  voxel_chunkmap_put(&world->chunks, key, chunk);

  if (chunk->mesh_dirty) {
    world_enqueue_dirty_chunk_key(world, key);
  }

  World_MarkNeighborsDirty(world, cx, cz);

  return chunk;
}

uint8_t World_GetBlock(const World *world, int x, int y, int z) {
  if (y < 0 || y >= WORLD_MAX_HEIGHT) {
    return BLOCK_AIR;
  }

  int cx = floor_div16(x);
  int cz = floor_div16(z);

  const Chunk *chunk = World_GetChunkConst(world, cx, cz);
  if (chunk == NULL) {
    return BLOCK_AIR;
  }

  int lx = floor_mod16(x);
  int lz = floor_mod16(z);

  return Chunk_GetBlock(chunk, lx, y, lz);
}

bool World_SetBlock(World *world, int x, int y, int z, uint8_t block_id) {
  if (y < 0 || y >= WORLD_MAX_HEIGHT) {
    return false;
  }

  int cx = floor_div16(x);
  int cz = floor_div16(z);
  int lx = floor_mod16(x);
  int lz = floor_mod16(z);

  Chunk *chunk = World_GetChunk(world, cx, cz);
  if (chunk == NULL) {
    return false;
  }

  uint8_t existing = Chunk_GetBlock(chunk, lx, y, lz);
  if (existing == block_id) {
    return false;
  }

  Chunk_SetBlock(chunk, lx, y, lz, block_id);
  Chunk_RecomputeHeightColumn(chunk, lx, lz);

  /* Direct block edits should always queue a remesh right away. */
  World_MarkChunkDirty(world, cx, cz);

  Lighting_RelightColumn(world, x, z);
  Lighting_RelightColumn(world, x + 1, z);
  Lighting_RelightColumn(world, x - 1, z);
  Lighting_RelightColumn(world, x, z + 1);
  Lighting_RelightColumn(world, x, z - 1);

  if (lx == 0 || lx == 15 || lz == 0 || lz == 15) {
    World_MarkNeighborsDirty(world, cx, cz);
  }

  Lighting_QueueAround(world, x, y, z);
  return true;
}

int World_GetTopY(const World *world, int x, int z) {
  int cx = floor_div16(x);
  int cz = floor_div16(z);

  const Chunk *chunk = World_GetChunkConst(world, cx, cz);
  if (chunk == NULL) {
    return 64;
  }

  int lx = floor_mod16(x);
  int lz = floor_mod16(z);
  return Chunk_GetHeight(chunk, lx, lz);
}

int World_GetSkyLight(const World *world, int x, int y, int z) {
  if (y < 0) {
    return 0;
  }
  if (y >= WORLD_MAX_HEIGHT) {
    return 15;
  }

  int cx = floor_div16(x);
  int cz = floor_div16(z);

  const Chunk *chunk = World_GetChunkConst(world, cx, cz);
  if (chunk == NULL) {
    return (y >= 64) ? 15 : 0;
  }

  int lx = floor_mod16(x);
  int lz = floor_mod16(z);
  return Chunk_GetSkyLight(chunk, lx, y, lz);
}

void World_SetSkyLight(World *world, int x, int y, int z, int value) {
  if (y < 0 || y >= WORLD_MAX_HEIGHT) {
    return;
  }

  int cx = floor_div16(x);
  int cz = floor_div16(z);

  Chunk *chunk = World_GetChunk(world, cx, cz);
  if (chunk == NULL) {
    return;
  }

  int lx = floor_mod16(x);
  int lz = floor_mod16(z);
  Chunk_SetSkyLight(chunk, lx, y, lz, value);
  World_MarkChunkDirty(world, cx, cz);
}

void World_MarkChunkDirty(World *world, int cx, int cz) {
  Chunk *chunk = World_GetChunk(world, cx, cz);
  if (chunk != NULL) {
    chunk->mesh_dirty = true;
    world_enqueue_dirty_chunk(world, cx, cz);
  }
}

void World_MarkNeighborsDirty(World *world, int cx, int cz) {
  World_MarkChunkDirty(world, cx, cz);
  World_MarkChunkDirty(world, cx + 1, cz);
  World_MarkChunkDirty(world, cx - 1, cz);
  World_MarkChunkDirty(world, cx, cz + 1);
  World_MarkChunkDirty(world, cx, cz - 1);
}

bool World_IsSolidAt(const World *world, int x, int y, int z) {
  uint8_t block_id = World_GetBlock(world, x, y, z);
  return Block_IsSolid(block_id);
}

bool World_CheckAABB(const World *world, BoundingBox box) {
  int min_x = (int)floorf(box.min.x + PHYSICS_EPSILON);
  int max_x = (int)floorf(box.max.x - PHYSICS_EPSILON);
  int min_y = (int)floorf(box.min.y + PHYSICS_EPSILON);
  int max_y = (int)floorf(box.max.y - PHYSICS_EPSILON);
  int min_z = (int)floorf(box.min.z + PHYSICS_EPSILON);
  int max_z = (int)floorf(box.max.z - PHYSICS_EPSILON);

  for (int x = min_x; x <= max_x; x++) {
    for (int y = min_y; y <= max_y; y++) {
      for (int z = min_z; z <= max_z; z++) {
        if (World_IsSolidAt(world, x, y, z)) {
          return true;
        }
      }
    }
  }

  return false;
}

bool World_CheckAABBForBlock(const World *world, BoundingBox box, uint8_t block_id) {
  int min_x = (int)floorf(box.min.x + PHYSICS_EPSILON);
  int max_x = (int)floorf(box.max.x - PHYSICS_EPSILON);
  int min_y = (int)floorf(box.min.y + PHYSICS_EPSILON);
  int max_y = (int)floorf(box.max.y - PHYSICS_EPSILON);
  int min_z = (int)floorf(box.min.z + PHYSICS_EPSILON);
  int max_z = (int)floorf(box.max.z - PHYSICS_EPSILON);

  for (int x = min_x; x <= max_x; x++) {
    for (int y = min_y; y <= max_y; y++) {
      for (int z = min_z; z <= max_z; z++) {
        if (World_GetBlock(world, x, y, z) == block_id) {
          return true;
        }
      }
    }
  }

  return false;
}

static BoundingBox liquid_query_box(BoundingBox box) {
  BoundingBox query = box;
  query.min.y += 0.4f;
  query.max.y -= 0.4f;
  return query;
}

bool World_IsAABBInWater(const World *world, BoundingBox box) {
  BoundingBox query = liquid_query_box(box);
  if (query.max.y <= query.min.y) {
    return false;
  }

  return World_CheckAABBForBlock(world, query, BLOCK_WATER);
}

bool World_IsAABBInLava(const World *world, BoundingBox box) {
  BoundingBox query = liquid_query_box(box);
  if (query.max.y <= query.min.y) {
    return false;
  }

  return World_CheckAABBForBlock(world, query, BLOCK_LAVA);
}

float World_GetAmbientMultiplier(const World *world) {
  float t = fmodf(world->world_time, 24000.0f) / 24000.0f;
  float phase = t - 0.25f;
  if (phase < 0.0f) {
    phase += 1.0f;
  }
  if (phase > 1.0f) {
    phase -= 1.0f;
  }

  float sky = cosf(phase * 6.28318530718f) * 0.5f + 0.5f;
  return Clamp(0.22f + sky * 0.78f, 0.12f, 1.0f);
}

static void update_ambient_darkness(World *world) {
  float ambient = World_GetAmbientMultiplier(world);
  int darkness = (int)((1.0f - ambient) * 11.0f);
  if (darkness < 0) {
    darkness = 0;
  }
  if (darkness > 11) {
    darkness = 11;
  }
  world->ambient_darkness = darkness;
}

static bool generate_next_chunk(World *world, int center_cx, int center_cz) {
  int rd = world->render_distance;

  for (int dist = 0; dist <= rd; dist++) {
    if (dist == 0) {
      if (World_GetChunk(world, center_cx, center_cz) == NULL) {
        World_GetOrCreateChunk(world, center_cx, center_cz);
        return true;
      }
      continue;
    }

    for (int dx = -dist; dx <= dist; dx++) {
      int x = center_cx + dx;

      int z0 = center_cz - dist;
      if (World_GetChunk(world, x, z0) == NULL) {
        World_GetOrCreateChunk(world, x, z0);
        return true;
      }

      int z1 = center_cz + dist;
      if (World_GetChunk(world, x, z1) == NULL) {
        World_GetOrCreateChunk(world, x, z1);
        return true;
      }
    }

    for (int dz = -dist + 1; dz <= dist - 1; dz++) {
      int z = center_cz + dz;

      int x0 = center_cx - dist;
      if (World_GetChunk(world, x0, z) == NULL) {
        World_GetOrCreateChunk(world, x0, z);
        return true;
      }

      int x1 = center_cx + dist;
      if (World_GetChunk(world, x1, z) == NULL) {
        World_GetOrCreateChunk(world, x1, z);
        return true;
      }
    }
  }

  return false;
}

static void unload_far_chunks(World *world, int center_cx, int center_cz) {
  int count = voxel_chunkmap_count(world->chunks);
  if (count == 0)
    return;

  int64_t *keys_to_remove = NULL;

  for (int i = 0; i < count; i++) {
    int64_t key = voxel_chunkmap_iter_key(world->chunks, i);
    int cx = (int)(key >> 32);
    int cz = (int)(key & 0xFFFFFFFF);

    int dx = cx - center_cx;
    int dz = cz - center_cz;
    int chebyshev_dist = (abs(dx) > abs(dz)) ? abs(dx) : abs(dz); /* max(|dx|, |dz|) */
    int unload_dist = world->render_distance + 2;

    if (chebyshev_dist > unload_dist) {
      WORLD_LOG("Marking chunk (%d, %d) for unload (chebyshev=%d, threshold=%d)", cx, cz,
                chebyshev_dist, unload_dist);
      arrput(keys_to_remove, key);
    }
  }

  ptrdiff_t remove_count = arrlen(keys_to_remove);
  if (remove_count > 0) {
    WORLD_LOG("Unloading %td chunks", remove_count);
  }

  for (ptrdiff_t i = 0; i < remove_count; i++) {
    Chunk *chunk = voxel_chunkmap_get(world->chunks, keys_to_remove[i]);
    if (chunk) {
      Chunk_Shutdown(chunk);
      free(chunk);
      voxel_chunkmap_remove(&world->chunks, keys_to_remove[i]);
    }
  }

  arrfree(keys_to_remove);
}

void World_Init(World *world, int64_t seed, int render_distance, Texture2D terrain_texture) {
  memset(world, 0, sizeof(*world));

  if (render_distance < 2) {
    render_distance = 2;
  }
  if (render_distance > 12) {
    render_distance = 12;
  }

  world->seed = seed;
  world->render_distance = render_distance;
  world->terrain_texture = terrain_texture;
  world->world_time = 6000.0f;

  world->chunks = voxel_chunkmap_create();

  Blocks_Init();
  WorldGen_Init(&world->generator, seed);

  LightUpdateQueue *queue = (LightUpdateQueue *)malloc(sizeof(LightUpdateQueue));
  if (queue != NULL) {
    LightQueue_Init(queue, 750000);
    world->light_queue_ptr = queue;
  }

  const float offset = 0.05f;
  for (int i = 0; i <= 15; i++) {
    float factor = 1.0f - (float)i / 15.0f;
    world->light_lut[i] = (1.0f - factor) / (factor * 3.0f + 1.0f) * (1.0f - offset) + offset;
  }

  for (int cz = -1; cz <= 1; cz++) {
    for (int cx = -1; cx <= 1; cx++) {
      World_GetOrCreateChunk(world, cx, cz);
    }
  }

  for (int i = 0; i < 12; i++) {
    Lighting_Process(world, 50000);
    const LightUpdateQueue *q = world_queue_const(world);
    if (q == NULL || q->count == 0) {
      break;
    }
  }

  update_ambient_darkness(world);
}

void World_Shutdown(World *world) {
  int count = voxel_chunkmap_count(world->chunks);
  for (int i = 0; i < count; i++) {
    Chunk *chunk = voxel_chunkmap_iter_value(world->chunks, i);
    if (chunk) {
      Chunk_Shutdown(chunk);
      free(chunk);
    }
  }

  voxel_chunkmap_destroy(&world->chunks);
  arrfree(world->dirty_chunk_keys);
  world->dirty_chunk_read_index = 0;
  hmfree(world->dirty_chunk_key_set);
  arrfree(g_solid_draw_entries);
  arrfree(g_translucent_draw_entries);
  arrfree(g_cutout_draw_entries);

  Mesher_Shutdown();

  if (world->light_queue_ptr != NULL) {
    LightUpdateQueue *queue = world_queue(world);
    LightQueue_Shutdown(queue);
    free(queue);
    world->light_queue_ptr = NULL;
  }

  WorldGen_Shutdown(&world->generator);
}

void World_Update(World *world, Vector3 player_pos, float dt) {
  Profiler_BeginSection("WorldUpdate");

  world->world_time += dt * 20.0f;
  update_ambient_darkness(world);

  int player_cx = floor_div16((int)floorf(player_pos.x));
  int player_cz = floor_div16((int)floorf(player_pos.z));

  Profiler_BeginSection("ChunkLoading");
  unload_far_chunks(world, player_cx, player_cz);

  for (int i = 0; i < 2; i++) {
    if (!generate_next_chunk(world, player_cx, player_cz)) {
      break;
    }
  }
  Profiler_EndSection();

  Profiler_BeginSection("Lighting");
  Lighting_Process(world, 50000);
  Profiler_EndSection();

  Profiler_BeginSection("ChunkMeshing");
  int rebuild_budget = 4;
  int rebuilt = 0;
  while (rebuild_budget > 0 && world->dirty_chunk_read_index < arrlen(world->dirty_chunk_keys)) {
    int64_t key = world->dirty_chunk_keys[world->dirty_chunk_read_index++];
    hmdel(world->dirty_chunk_key_set, key);
    Chunk *chunk = voxel_chunkmap_get(world->chunks, key);
    if (chunk && chunk->mesh_dirty) {
      WORLD_LOG("Rebuilding chunk (%d, %d)", chunk->cx, chunk->cz);
      Mesher_RebuildChunk(world, chunk, 1.0f);
      chunk->mesh_dirty = false;
      rebuild_budget--;
      rebuilt++;
    }
  }
  world_compact_dirty_chunk_queue(world);

  if (rebuilt > 0) {
    WORLD_LOG("Total rebuilt: %d chunks", rebuilt);
  }
  Profiler_EndSection();

  Profiler_EndSection(); // WorldUpdate
}

struct ChunkSortEntry {
  Chunk *chunk;
  float dist_sq;
};

static int compare_chunk_solid(const void *a, const void *b) {
  const ChunkSortEntry *ca = a;
  const ChunkSortEntry *cb = b;
  if (ca->dist_sq < cb->dist_sq)
    return -1;
  if (ca->dist_sq > cb->dist_sq)
    return 1;
  return 0;
}

static int compare_chunk_translucent(const void *a, const void *b) {
  const ChunkSortEntry *ca = a;
  const ChunkSortEntry *cb = b;
  if (ca->dist_sq > cb->dist_sq)
    return -1;
  if (ca->dist_sq < cb->dist_sq)
    return 1;
  return 0;
}

void World_Draw(World *world, const Camera3D *camera, float ambient_multiplier) {
  int cam_cx = floor_div16((int)floorf(camera->position.x));
  int cam_cz = floor_div16((int)floorf(camera->position.z));
  int rd = world->render_distance + 1;

  float cam_x = camera->position.x;
  float cam_z = camera->position.z;

  unsigned char ambient_channel = (unsigned char)(Clamp(ambient_multiplier, 0.15f, 1.0f) * 255.0f);
  Color solid_tint = {ambient_channel, ambient_channel, ambient_channel, 255};
  Color translucent_tint = {ambient_channel, ambient_channel, ambient_channel, 180};
  Color cutout_tint = {ambient_channel, ambient_channel, ambient_channel, 255};

  arrsetlen(g_solid_draw_entries, 0);
  arrsetlen(g_translucent_draw_entries, 0);
  arrsetlen(g_cutout_draw_entries, 0);

  int chunk_count = voxel_chunkmap_count(world->chunks);
  for (int i = 0; i < chunk_count; i++) {
    Chunk *chunk = voxel_chunkmap_iter_value(world->chunks, i);
    if (!chunk)
      continue;

    int dx = chunk->cx - cam_cx;
    int dz = chunk->cz - cam_cz;
    if (abs(dx) > rd || abs(dz) > rd) {
      continue;
    }

    float chunk_center_x = (float)(chunk->cx * WORLD_CHUNK_SIZE_X) + WORLD_CHUNK_SIZE_X * 0.5f;
    float chunk_center_z = (float)(chunk->cz * WORLD_CHUNK_SIZE_Z) + WORLD_CHUNK_SIZE_Z * 0.5f;
    float dist_sq = (chunk_center_x - cam_x) * (chunk_center_x - cam_x) +
                    (chunk_center_z - cam_z) * (chunk_center_z - cam_z);

    if (chunk->has_solid_model) {
      arrput(g_solid_draw_entries, ((ChunkSortEntry){.chunk = chunk, .dist_sq = dist_sq}));
    }

    if (chunk->has_translucent_model) {
      arrput(g_translucent_draw_entries, ((ChunkSortEntry){.chunk = chunk, .dist_sq = dist_sq}));
    }

    if (chunk->has_cutout_model) {
      arrput(g_cutout_draw_entries, ((ChunkSortEntry){.chunk = chunk, .dist_sq = dist_sq}));
    }
  }

  ptrdiff_t solid_count = arrlen(g_solid_draw_entries);
  ptrdiff_t translucent_count = arrlen(g_translucent_draw_entries);
  ptrdiff_t cutout_count = arrlen(g_cutout_draw_entries);

  if (solid_count > 1) {
    qsort(g_solid_draw_entries, (size_t)solid_count, sizeof(ChunkSortEntry), compare_chunk_solid);
  }
  if (translucent_count > 1) {
    qsort(g_translucent_draw_entries, (size_t)translucent_count, sizeof(ChunkSortEntry),
          compare_chunk_translucent);
  }
  if (cutout_count > 1) {
    qsort(g_cutout_draw_entries, (size_t)cutout_count, sizeof(ChunkSortEntry),
          compare_chunk_translucent);
  }

  for (ptrdiff_t i = 0; i < solid_count; i++) {
    Chunk *chunk = g_solid_draw_entries[i].chunk;
    Vector3 chunk_pos = {(float)(chunk->cx * WORLD_CHUNK_SIZE_X), 0.0f,
                         (float)(chunk->cz * WORLD_CHUNK_SIZE_Z)};
    DrawModel(*(Model *)chunk->solid_model, chunk_pos, 1.0f, solid_tint);
  }

  /* Disable backface culling for translucent (water) and cutout (plants) */
  rlDisableBackfaceCulling();

  for (ptrdiff_t i = 0; i < translucent_count; i++) {
    Chunk *chunk = g_translucent_draw_entries[i].chunk;
    Vector3 chunk_pos = {(float)(chunk->cx * WORLD_CHUNK_SIZE_X), 0.0f,
                         (float)(chunk->cz * WORLD_CHUNK_SIZE_Z)};
    DrawModel(*(Model *)chunk->translucent_model, chunk_pos, 1.0f, translucent_tint);
  }

  for (ptrdiff_t i = 0; i < cutout_count; i++) {
    Chunk *chunk = g_cutout_draw_entries[i].chunk;
    Vector3 chunk_pos = {(float)(chunk->cx * WORLD_CHUNK_SIZE_X), 0.0f,
                         (float)(chunk->cz * WORLD_CHUNK_SIZE_Z)};
    DrawModel(*(Model *)chunk->cutout_model, chunk_pos, 1.0f, cutout_tint);
  }

  rlEnableBackfaceCulling();
}
