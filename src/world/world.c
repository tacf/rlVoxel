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

/*
 * Lighting scheduler tuning.
 *
 * Units:
 * - *_NODES_PER_TICK_*: max light nodes processed by Lighting_Process per tick.
 * - *_THRESHOLD: pending nodes currently in LightUpdateQueue.
 *
 * Behavior:
 * - When queue backlog is high, we process more light nodes and load fewer chunks.
 * - On direct block edits, we do a small immediate settle pass to reduce visible
 *   one-tick lighting artifacts around the edited block.
 */
#define WORLD_LIGHT_NODES_PER_TICK_BASE 50000
#define WORLD_LIGHT_NODES_PER_TICK_BACKLOG 90000
#define WORLD_LIGHT_NODES_PER_TICK_OVERLOAD 130000
#define WORLD_LIGHT_QUEUE_BACKLOG_THRESHOLD 550000
#define WORLD_LIGHT_QUEUE_OVERLOAD_THRESHOLD 700000
#define WORLD_LIGHT_QUEUE_CAPACITY_HEADROOM 65536
#define WORLD_LIGHT_NODES_ON_BLOCK_EDIT 4096

typedef struct ChunkSortEntry ChunkSortEntry;
static ChunkSortEntry *g_solid_draw_entries = NULL;
static ChunkSortEntry *g_translucent_draw_entries = NULL;
static ChunkSortEntry *g_cutout_draw_entries = NULL;
static ChunkSortEntry *g_translucent_solid_draw_entries = NULL;

static inline int64_t make_chunk_key(int cx, int cz) { return ((int64_t)cx << 32) | (uint32_t)cz; }

enum {
  WORLD_DIRTY_QUEUE_NORMAL = 1,
  WORLD_DIRTY_QUEUE_PRIORITY = 2,
};

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

static void world_enqueue_dirty_chunk_key(World *world, int64_t key, bool priority) {
  int entry_index;
  unsigned char queue_state;

  if (world == NULL) {
    return;
  }

  queue_state = priority ? WORLD_DIRTY_QUEUE_PRIORITY : WORLD_DIRTY_QUEUE_NORMAL;
  entry_index = hmgeti(world->dirty_chunk_key_set, key);
  if (entry_index >= 0) {
    if (priority && world->dirty_chunk_key_set[entry_index].value == WORLD_DIRTY_QUEUE_NORMAL) {
      world->dirty_chunk_key_set[entry_index].value = WORLD_DIRTY_QUEUE_PRIORITY;
      arrput(world->dirty_chunk_priority_keys, key);
    }
    return;
  }
  hmput(world->dirty_chunk_key_set, key, queue_state);
  if (priority) {
    arrput(world->dirty_chunk_priority_keys, key);
  } else {
    arrput(world->dirty_chunk_keys, key);
  }
}

static void world_enqueue_dirty_chunk(World *world, int cx, int cz, bool priority) {
  world_enqueue_dirty_chunk_key(world, make_chunk_key(cx, cz), priority);
}

static void world_compact_dirty_chunk_queue(int64_t **queue, ptrdiff_t *read_index) {
  ptrdiff_t len;

  if (queue == NULL || read_index == NULL || *queue == NULL) {
    return;
  }

  len = arrlen(*queue);
  if (*read_index <= 0) {
    return;
  }

  if (*read_index >= len) {
    arrsetlen(*queue, 0);
    *read_index = 0;
    return;
  }

  if (*read_index > 1024 && *read_index * 2 >= len) {
    arrdeln(*queue, 0, *read_index);
    *read_index = 0;
  }
}

static bool world_dequeue_dirty_chunk_key(World *world, int64_t *out_key) {
  ptrdiff_t entry_index;
  int64_t key;

  if (world == NULL || out_key == NULL) {
    return false;
  }

  while (world->dirty_chunk_priority_read_index < arrlen(world->dirty_chunk_priority_keys)) {
    key = world->dirty_chunk_priority_keys[world->dirty_chunk_priority_read_index++];
    entry_index = hmgeti(world->dirty_chunk_key_set, key);
    if (entry_index < 0) {
      continue;
    }
    if (world->dirty_chunk_key_set[entry_index].value != WORLD_DIRTY_QUEUE_PRIORITY) {
      continue;
    }
    hmdel(world->dirty_chunk_key_set, key);
    *out_key = key;
    return true;
  }

  while (world->dirty_chunk_read_index < arrlen(world->dirty_chunk_keys)) {
    key = world->dirty_chunk_keys[world->dirty_chunk_read_index++];
    entry_index = hmgeti(world->dirty_chunk_key_set, key);
    if (entry_index < 0) {
      continue;
    }
    if (world->dirty_chunk_key_set[entry_index].value != WORLD_DIRTY_QUEUE_NORMAL) {
      continue;
    }
    hmdel(world->dirty_chunk_key_set, key);
    *out_key = key;
    return true;
  }

  return false;
}

static void world_relight_columns_around(World *world, int x, int z, int radius) {
  for (int dz = -radius; dz <= radius; dz++) {
    for (int dx = -radius; dx <= radius; dx++) {
      Lighting_RelightColumn(world, x + dx, z + dz);
    }
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
  if (world->authoritative_mode) {
    WorldGen_GenerateChunk(&world->generator, chunk);
    Lighting_InitializeChunkSkylight(world, chunk);
  } else {
    chunk->generated = true;
  }

  int64_t key = make_chunk_key(cx, cz);
  voxel_chunkmap_put(&world->chunks, key, chunk);

  if (world->meshing_enabled && chunk->mesh_dirty) {
    world_enqueue_dirty_chunk_key(world, key, false);
  }

  if (world->meshing_enabled) {
    World_MarkNeighborsDirty(world, cx, cz);
  }

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

  if (world->meshing_enabled) {
    /* Direct block edits should always queue a remesh right away. */
    World_MarkChunkDirtyPriority(world, cx, cz);
  }

  if (world->authoritative_mode) {
    /*
     * Relight a 3x3 neighborhood around the edit to quickly repair stale side
     * lighting and avoid dark side-faces after local block edits.
     */
    world_relight_columns_around(world, x, z, 1);
  }

  if (world->meshing_enabled && (lx == 0 || lx == 15 || lz == 0 || lz == 15)) {
    World_MarkNeighborsDirtyPriority(world, cx, cz);
  }

  if (world->authoritative_mode) {
    Lighting_QueueAround(world, x, y, z);
    Lighting_Process(world, WORLD_LIGHT_NODES_ON_BLOCK_EDIT);
  }
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
  if (world->meshing_enabled) {
    World_MarkChunkDirty(world, cx, cz);
  }
}

void World_MarkChunkDirty(World *world, int cx, int cz) {
  Chunk *chunk = World_GetChunk(world, cx, cz);
  if (chunk != NULL) {
    if (world->meshing_enabled) {
      chunk->mesh_dirty = true;
      world_enqueue_dirty_chunk(world, cx, cz, false);
    }
  }
}

void World_MarkChunkDirtyPriority(World *world, int cx, int cz) {
  Chunk *chunk = World_GetChunk(world, cx, cz);
  if (chunk != NULL) {
    if (world->meshing_enabled) {
      chunk->mesh_dirty = true;
      world_enqueue_dirty_chunk(world, cx, cz, true);
    }
  }
}

void World_MarkNeighborsDirty(World *world, int cx, int cz) {
  World_MarkChunkDirty(world, cx, cz);
  World_MarkChunkDirty(world, cx + 1, cz);
  World_MarkChunkDirty(world, cx - 1, cz);
  World_MarkChunkDirty(world, cx, cz + 1);
  World_MarkChunkDirty(world, cx, cz - 1);
}

void World_MarkNeighborsDirtyPriority(World *world, int cx, int cz) {
  World_MarkChunkDirtyPriority(world, cx, cz);
  World_MarkChunkDirtyPriority(world, cx + 1, cz);
  World_MarkChunkDirtyPriority(world, cx - 1, cz);
  World_MarkChunkDirtyPriority(world, cx, cz + 1);
  World_MarkChunkDirtyPriority(world, cx, cz - 1);
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

static int world_chunk_generation_budget(const World *world) {
  const LightUpdateQueue *queue = world_queue_const(world);
  if (queue == NULL) {
    return 2;
  }
  if (queue->count >= WORLD_LIGHT_QUEUE_OVERLOAD_THRESHOLD) {
    return 0;
  }
  if (queue->count >= WORLD_LIGHT_QUEUE_BACKLOG_THRESHOLD) {
    return 1;
  }
  return 2;
}

static int world_lighting_budget(const World *world) {
  const LightUpdateQueue *queue = world_queue_const(world);
  if (queue == NULL) {
    return WORLD_LIGHT_NODES_PER_TICK_BASE;
  }
  if (queue->count >= WORLD_LIGHT_QUEUE_OVERLOAD_THRESHOLD) {
    return WORLD_LIGHT_NODES_PER_TICK_OVERLOAD;
  }
  if (queue->count >= WORLD_LIGHT_QUEUE_BACKLOG_THRESHOLD) {
    return WORLD_LIGHT_NODES_PER_TICK_BACKLOG;
  }
  return WORLD_LIGHT_NODES_PER_TICK_BASE;
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

static void world_init_internal(World *world, int64_t seed, int render_distance,
                                Texture2D terrain_texture, bool authoritative_mode,
                                bool meshing_enabled, bool warmup) {
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
  world->authoritative_mode = authoritative_mode;
  world->meshing_enabled = meshing_enabled;

  world->chunks = voxel_chunkmap_create();

  Blocks_Init();
  if (authoritative_mode) {
    WorldGen_Init(&world->generator, seed);
  }

  if (authoritative_mode) {
    LightUpdateQueue *queue = (LightUpdateQueue *)malloc(sizeof(LightUpdateQueue));
    if (queue != NULL) {
      LightQueue_Init(queue,
                      WORLD_LIGHT_QUEUE_OVERLOAD_THRESHOLD + WORLD_LIGHT_QUEUE_CAPACITY_HEADROOM);
      world->light_queue_ptr = queue;
    }
  }

  const float offset = 0.05f;
  for (int i = 0; i <= 15; i++) {
    float factor = 1.0f - (float)i / 15.0f;
    world->light_lut[i] = (1.0f - factor) / (factor * 3.0f + 1.0f) * (1.0f - offset) + offset;
  }

  if (authoritative_mode && warmup) {
    for (int cz = -1; cz <= 1; cz++) {
      for (int cx = -1; cx <= 1; cx++) {
        World_GetOrCreateChunk(world, cx, cz);
      }
    }

    for (int i = 0; i < 12; i++) {
      Lighting_Process(world, world_lighting_budget(world));
      const LightUpdateQueue *q = world_queue_const(world);
      if (q == NULL || q->count == 0) {
        break;
      }
    }
  }

  update_ambient_darkness(world);
}

void World_Init(World *world, int64_t seed, int render_distance, Texture2D terrain_texture) {
  world_init_internal(world, seed, render_distance, terrain_texture, true, true, true);
}

void World_InitServer(World *world, int64_t seed, int render_distance) {
  world_init_internal(world, seed, render_distance, (Texture2D){0}, true, false, true);
}

void World_InitReplicated(World *world, int render_distance, Texture2D terrain_texture) {
  world_init_internal(world, 0, render_distance, terrain_texture, false, true, false);
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
  arrfree(world->dirty_chunk_priority_keys);
  world->dirty_chunk_priority_read_index = 0;
  arrfree(world->dirty_chunk_keys);
  world->dirty_chunk_read_index = 0;
  hmfree(world->dirty_chunk_key_set);
  if (world->meshing_enabled) {
    arrfree(g_solid_draw_entries);
    arrfree(g_translucent_draw_entries);
    arrfree(g_cutout_draw_entries);
    arrfree(g_translucent_solid_draw_entries);
    Mesher_Shutdown();
  }

  if (world->light_queue_ptr != NULL) {
    LightUpdateQueue *queue = world_queue(world);
    LightQueue_Shutdown(queue);
    free(queue);
    world->light_queue_ptr = NULL;
  }

  if (world->authoritative_mode) {
    WorldGen_Shutdown(&world->generator);
  }
}

void World_Update(World *world, Vector3 player_pos, float dt) {
  Profiler_BeginSection("WorldUpdate");

  if (world->authoritative_mode) {
    world->world_time += dt * 20.0f;
    update_ambient_darkness(world);

    int player_cx = floor_div16((int)floorf(player_pos.x));
    int player_cz = floor_div16((int)floorf(player_pos.z));

    Profiler_BeginSection("ChunkLoading");
    unload_far_chunks(world, player_cx, player_cz);

    int chunk_generation_budget = world_chunk_generation_budget(world);
    for (int i = 0; i < chunk_generation_budget; i++) {
      if (!generate_next_chunk(world, player_cx, player_cz)) {
        break;
      }
    }
    Profiler_EndSection();

    Profiler_BeginSection("Lighting");
    Lighting_Process(world, world_lighting_budget(world));
    Profiler_EndSection();
  } else {
    update_ambient_darkness(world);
  }

  if (world->meshing_enabled) {
    int rebuild_budget = 0;
    Profiler_BeginSection("ChunkMeshing");
    /*
     * Client-replicated worlds avoid bursty remesh work while streaming chunks.
     * We only permit one remesh every other simulation tick.
     */
    if (world->authoritative_mode) {
      rebuild_budget = 4;
    } else if (world->replicated_mesh_skip_ticks > 0) {
      world->replicated_mesh_skip_ticks--;
    } else {
      rebuild_budget = 1;
      world->replicated_mesh_skip_ticks = 1;
    }
    int rebuilt = 0;
    while (rebuild_budget > 0) {
      int64_t key;
      if (!world_dequeue_dirty_chunk_key(world, &key)) {
        break;
      }
      Chunk *chunk = voxel_chunkmap_get(world->chunks, key);
      if (chunk && chunk->mesh_dirty) {
        WORLD_LOG("Rebuilding chunk (%d, %d)", chunk->cx, chunk->cz);
        Mesher_RebuildChunk(world, chunk, 1.0f);
        chunk->mesh_dirty = false;
        rebuild_budget--;
        rebuilt++;
      }
    }
    world_compact_dirty_chunk_queue(&world->dirty_chunk_priority_keys,
                                    &world->dirty_chunk_priority_read_index);
    world_compact_dirty_chunk_queue(&world->dirty_chunk_keys, &world->dirty_chunk_read_index);

    if (rebuilt > 0) {
      WORLD_LOG("Total rebuilt: %d chunks", rebuilt);
    }
    Profiler_EndSection();
  }

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

static void collect_visible_chunks(World *world, int cam_cx, int cam_cz, float cam_x, float cam_z,
                                   int rd) {
  arrsetlen(g_solid_draw_entries, 0);
  arrsetlen(g_translucent_draw_entries, 0);
  arrsetlen(g_cutout_draw_entries, 0);
  arrsetlen(g_translucent_solid_draw_entries, 0);

  int chunk_count = voxel_chunkmap_count(world->chunks);
  for (int i = 0; i < chunk_count; i++) {
    Chunk *chunk = voxel_chunkmap_iter_value(world->chunks, i);
    if (!chunk)
      continue;

    int dx = chunk->cx - cam_cx;
    int dz = chunk->cz - cam_cz;
    if (abs(dx) > rd || abs(dz) > rd)
      continue;

    float cx = (float)(chunk->cx * WORLD_CHUNK_SIZE_X) + WORLD_CHUNK_SIZE_X * 0.5f;
    float cz = (float)(chunk->cz * WORLD_CHUNK_SIZE_Z) + WORLD_CHUNK_SIZE_Z * 0.5f;
    float dist_sq = (cx - cam_x) * (cx - cam_x) + (cz - cam_z) * (cz - cam_z);

    if (chunk->has_solid_model && chunk->solid_model)
      arrput(g_solid_draw_entries, ((ChunkSortEntry){chunk, dist_sq}));
    if (chunk->has_cutout_model && chunk->cutout_model)
      arrput(g_cutout_draw_entries, ((ChunkSortEntry){chunk, dist_sq}));
    if (chunk->has_translucent_solid_model && chunk->translucent_solid_model)
      arrput(g_translucent_solid_draw_entries, ((ChunkSortEntry){chunk, dist_sq}));
    if (chunk->has_translucent_model && chunk->translucent_model)
      arrput(g_translucent_draw_entries, ((ChunkSortEntry){chunk, dist_sq}));
  }
}

static void sort_chunks(ChunkSortEntry *entries, ptrdiff_t count, bool front_to_back) {
  if (count > 1) {
    qsort(entries, (size_t)count, sizeof(ChunkSortEntry),
          front_to_back ? compare_chunk_solid : compare_chunk_translucent);
  }
}

/*
 * Render pipeline: see docs/RENDERING.md for full documentation on
 * pass architecture, GL state, shaders, and raylib integration.
 */
static void draw_chunk_models(ChunkSortEntry *entries, ptrdiff_t count, size_t model_offset,
                              Color tint) {
  for (ptrdiff_t i = 0; i < count; i++) {
    Chunk *chunk = entries[i].chunk;
    Model *model = *(Model **)((char *)chunk + model_offset);
    if (model == NULL)
      continue;
    Vector3 pos = {(float)(chunk->cx * WORLD_CHUNK_SIZE_X), 0.0f,
                   (float)(chunk->cz * WORLD_CHUNK_SIZE_Z)};
    DrawModel(*model, pos, 1.0f, tint);
  }
}

static Color make_ambient_tint(unsigned char ambient, unsigned char alpha) {
  return (Color){ambient, ambient, ambient, alpha};
}

/*
 * Render pass 1: SOLID
 * Depth: read+write. Blend: none. Cull: back faces. Sort: front-to-back.
 */
static void draw_solid_pass(ChunkSortEntry *entries, ptrdiff_t count, Color tint) {
  draw_chunk_models(entries, count, offsetof(Chunk, solid_model), tint);
}

/*
 * Render pass 2: CUTOUT (leaves, plants)
 * Depth: read+write. Blend: none (alpha discard in shader). Cull: off. Sort: back-to-front.
 */
static void draw_cutout_pass(ChunkSortEntry *entries, ptrdiff_t count, Color tint) {
  rlDisableBackfaceCulling();
  draw_chunk_models(entries, count, offsetof(Chunk, cutout_model), tint);
  rlEnableBackfaceCulling();
}

/*
 * Render pass 3: TRANSLUCENT SOLID (ice, glass)
 * Depth: read+write. Blend: alpha. Cull: off. Sort: front-to-back.
 */
static void draw_translucent_solid_pass(ChunkSortEntry *entries, ptrdiff_t count, Color tint) {
  rlDisableBackfaceCulling();
  draw_chunk_models(entries, count, offsetof(Chunk, translucent_solid_model), tint);
  rlEnableBackfaceCulling();
}

/*
 * Render pass 4: WATER (no depth write)
 * Depth: read only. Blend: alpha. Cull: off. Sort: back-to-front.
 */
static void draw_water_pass(ChunkSortEntry *entries, ptrdiff_t count, Color tint) {
  rlDisableBackfaceCulling();
  rlDisableDepthMask();
  draw_chunk_models(entries, count, offsetof(Chunk, translucent_model), tint);
  rlEnableDepthMask();
  rlEnableBackfaceCulling();
}

void World_Draw(World *world, const Camera3D *camera, float ambient_multiplier) {
  if (world == NULL || camera == NULL || !world->meshing_enabled) {
    return;
  }

  int cam_cx = floor_div16((int)floorf(camera->position.x));
  int cam_cz = floor_div16((int)floorf(camera->position.z));
  int rd = world->render_distance + 1;

  unsigned char a = (unsigned char)(Clamp(ambient_multiplier, 0.15f, 1.0f) * 255.0f);
  Color solid_tint = make_ambient_tint(a, 255);
  Color cutout_tint = make_ambient_tint(a, 255);
  Color translucent_solid_tint = make_ambient_tint(a, 255);
  Color water_tint = make_ambient_tint(a, 180);

  collect_visible_chunks(world, cam_cx, cam_cz, camera->position.x, camera->position.z, rd);

  ptrdiff_t solid_count = arrlen(g_solid_draw_entries);
  ptrdiff_t cutout_count = arrlen(g_cutout_draw_entries);
  ptrdiff_t tsolid_count = arrlen(g_translucent_solid_draw_entries);
  ptrdiff_t water_count = arrlen(g_translucent_draw_entries);

  sort_chunks(g_solid_draw_entries, solid_count, true);
  sort_chunks(g_cutout_draw_entries, cutout_count, false);
  sort_chunks(g_translucent_solid_draw_entries, tsolid_count, true);
  sort_chunks(g_translucent_draw_entries, water_count, false);

  draw_solid_pass(g_solid_draw_entries, solid_count, solid_tint);
  draw_cutout_pass(g_cutout_draw_entries, cutout_count, cutout_tint);
  draw_translucent_solid_pass(g_translucent_solid_draw_entries, tsolid_count,
                              translucent_solid_tint);
  draw_water_pass(g_translucent_draw_entries, water_count, water_tint);
}

bool World_ApplyChunkData(World *world, int cx, int cz, const uint8_t *blocks, size_t blocks_size,
                          const uint8_t *skylight, size_t skylight_size, const uint8_t *heightmap,
                          size_t heightmap_size) {
  Chunk *chunk;
  int64_t key;

  if (world == NULL || blocks == NULL || skylight == NULL || heightmap == NULL) {
    return false;
  }

  if (blocks_size != WORLD_CHUNK_VOLUME || skylight_size != WORLD_CHUNK_LIGHT_BYTES ||
      heightmap_size != (WORLD_CHUNK_SIZE_X * WORLD_CHUNK_SIZE_Z)) {
    return false;
  }

  chunk = World_GetChunk(world, cx, cz);
  if (chunk == NULL) {
    chunk = (Chunk *)calloc(1, sizeof(Chunk));
    if (chunk == NULL) {
      return false;
    }
    Chunk_Init(chunk, cx, cz);
    key = make_chunk_key(cx, cz);
    voxel_chunkmap_put(&world->chunks, key, chunk);
  }

  memcpy(chunk->blocks, blocks, blocks_size);
  memcpy(chunk->skylight, skylight, skylight_size);
  memcpy(chunk->heightmap, heightmap, heightmap_size);
  chunk->generated = true;
  chunk->lighting_dirty = false;
  chunk->mesh_dirty = world->meshing_enabled;

  if (world->meshing_enabled) {
    World_MarkChunkDirty(world, cx, cz);
    World_MarkNeighborsDirty(world, cx, cz);
  }

  return true;
}

bool World_ApplyBlockDelta(World *world, int x, int y, int z, uint8_t block_id, uint8_t skylight) {
  int cx, cz, lx, lz;
  Chunk *chunk;

  if (world == NULL || y < 0 || y >= WORLD_MAX_HEIGHT) {
    return false;
  }

  cx = floor_div16(x);
  cz = floor_div16(z);
  lx = floor_mod16(x);
  lz = floor_mod16(z);
  chunk = World_GetChunk(world, cx, cz);
  if (chunk == NULL) {
    return false;
  }

  Chunk_SetBlock(chunk, lx, y, lz, block_id);
  Chunk_SetSkyLight(chunk, lx, y, lz, skylight);
  Chunk_RecomputeHeightColumn(chunk, lx, lz);

  if (world->meshing_enabled) {
    World_MarkChunkDirtyPriority(world, cx, cz);
    if (lx == 0 || lx == 15 || lz == 0 || lz == 15) {
      World_MarkNeighborsDirtyPriority(world, cx, cz);
    }
  }

  return true;
}

bool World_RemoveChunk(World *world, int cx, int cz) {
  int64_t key;
  Chunk *chunk;

  if (world == NULL) {
    return false;
  }

  key = make_chunk_key(cx, cz);
  chunk = voxel_chunkmap_get(world->chunks, key);
  if (chunk == NULL) {
    return false;
  }

  Chunk_Shutdown(chunk);
  free(chunk);
  voxel_chunkmap_remove(&world->chunks, key);
  hmdel(world->dirty_chunk_key_set, key);
  return true;
}

void World_SetTime(World *world, float world_time) {
  if (world == NULL) {
    return;
  }
  world->world_time = world_time;
  update_ambient_darkness(world);
}
