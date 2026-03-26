#ifndef RLVOXEL_WORLD_H
#define RLVOXEL_WORLD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <raylib.h>
#include <voxel/chunkmap.h>

#include "world/chunk.h"
#include "world/lighting.h"
#include "world/worldgen.h"

/**
 * The World manages all chunks and provides block-level operations.
 * It handles chunk loading/unloading, lighting updates, and rendering.
 */
typedef struct World {
  /** Hash map of loaded chunks */
  VoxelChunkMap chunks;

  /** Number of chunks to render in each direction from player */
  int render_distance;

  /** World seed for terrain generation */
  int64_t seed;

  /** Terrain generator */
  WorldGen generator;

  /** Queue for pending lighting updates */
  LightUpdateQueue *light_queue_ptr;

  /** FIFO of dirty chunk keys pending remesh processing */
  int64_t *dirty_chunk_keys;
  ptrdiff_t dirty_chunk_read_index;

  /** Set of chunk keys currently queued for remesh */
  struct {
    int64_t key;
    unsigned char value;
  } *dirty_chunk_key_set;

  /** Terrain texture atlas */
  Texture2D terrain_texture;

  /** Light level look-up table for ambient occlusion */
  float light_lut[16];

  /** Current world time (in ticks, cycles every 24000) */
  float world_time;

  /** Base ambient light level (0-15) */
  int ambient_darkness;

  /** True when this world performs simulation-side chunk management/generation. */
  bool authoritative_mode;

  /** True when this world runs meshing and supports draw passes. */
  bool meshing_enabled;

  /** Client-only remesh throttle state (0 = allow remesh this tick). */
  uint8_t replicated_mesh_skip_ticks;
} World;

/**
 * Initializes a new world with the given seed.
 * @param seed Random seed for terrain generation
 * @param render_distance Chunks to render around player
 * @param terrain_texture The texture atlas to use for blocks
 */
void World_Init(World *world, int64_t seed, int render_distance, Texture2D terrain_texture);

/**
 * Initializes an authoritative server world without meshing/draw work.
 */
void World_InitServer(World *world, int64_t seed, int render_distance);

/**
 * Initializes a client-side replicated world (no procedural generation/loading).
 */
void World_InitReplicated(World *world, int render_distance, Texture2D terrain_texture);

/**
 * Shuts down the world and frees all resources.
 */
void World_Shutdown(World *world);

/**
 * Updates the world state each simulation tick.
 * Handles chunk loading/unloading around the player.
 */
void World_Update(World *world, Vector3 player_pos, float dt);

/**
 * Renders all visible chunks.
 * @param camera The camera to render from
 * @param ambient_multiplier Brightness multiplier for lighting
 */
void World_Draw(World *world, const Camera3D *camera, float ambient_multiplier);

/**
 * Gets a chunk by coordinates, or NULL if not loaded.
 */
Chunk *World_GetChunk(World *world, int cx, int cz);
const Chunk *World_GetChunkConst(const World *world, int cx, int cz);

/**
 * Gets an existing chunk or creates a new one if not loaded.
 */
Chunk *World_GetOrCreateChunk(World *world, int cx, int cz);

/**
 * Gets the block ID at world coordinates.
 * Returns BLOCK_AIR if coordinates are out of bounds.
 */
uint8_t World_GetBlock(const World *world, int x, int y, int z);

/**
 * Sets the block ID at world coordinates.
 * Marks chunk as dirty for remeshing.
 * @return true if block was successfully set
 */
bool World_SetBlock(World *world, int x, int y, int z, uint8_t block_id);

/**
 * Gets the height of the highest solid block at x,z.
 */
int World_GetTopY(const World *world, int x, int z);

/**
 * Gets the skylight value (0-15) at coordinates.
 */
int World_GetSkyLight(const World *world, int x, int y, int z);

/**
 * Sets the skylight value (0-15) at coordinates.
 */
void World_SetSkyLight(World *world, int x, int y, int z, int value);

/**
 * Marks a chunk as needing mesh rebuild.
 */
void World_MarkChunkDirty(World *world, int cx, int cz);

/**
 * Marks a chunk and its 4 neighbors as needing mesh rebuild.
 */
void World_MarkNeighborsDirty(World *world, int cx, int cz);

/**
 * Checks if a block position contains a solid block.
 */
bool World_IsSolidAt(const World *world, int x, int y, int z);

/**
 * Checks if an axis-aligned bounding box intersects any solid block.
 */
bool World_CheckAABB(const World *world, BoundingBox box);

/**
 * Checks if an axis-aligned bounding box contains a specific block type.
 */
bool World_CheckAABBForBlock(const World *world, BoundingBox box, uint8_t block_id);

/**
 * Checks if an axis-aligned bounding box intersects water.
 */
bool World_IsAABBInWater(const World *world, BoundingBox box);

/**
 * Checks if an axis-aligned bounding box intersects lava.
 */
bool World_IsAABBInLava(const World *world, BoundingBox box);

/**
 * Gets the ambient light multiplier based on time of day.
 * Returns value between 0.2 and 1.0
 */
float World_GetAmbientMultiplier(const World *world);

/**
 * Applies network chunk payload data to a chunk, creating the chunk if needed.
 */
bool World_ApplyChunkData(World *world, int cx, int cz, const uint8_t *blocks, size_t blocks_size,
                          const uint8_t *skylight, size_t skylight_size, const uint8_t *heightmap,
                          size_t heightmap_size);

/**
 * Applies a network block delta update in replicated mode.
 */
bool World_ApplyBlockDelta(World *world, int x, int y, int z, uint8_t block_id, uint8_t skylight);

/**
 * Removes a chunk from the world map (for chunk unload replication).
 */
bool World_RemoveChunk(World *world, int cx, int cz);

/**
 * Overrides world time from authoritative snapshots.
 */
void World_SetTime(World *world, float world_time);

#endif /* RLVOXEL_WORLD_H */
