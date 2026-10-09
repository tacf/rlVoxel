#include "world/lighting.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "stb_ds.h"
#include "constants.h"
#include "voxel/chunk.h"
#include "world/blocks.h"
#include "world/chunk.h"
#include "world/world.h"

void LightQueue_Init(LightUpdateQueue *queue, int capacity) {
  memset(queue, 0, sizeof(*queue));
  if (capacity < 1024) {
    capacity = 1024;
  }
  queue->max_pending = capacity;
  arrsetlen(queue->items, (size_t)capacity);
}

void LightQueue_Shutdown(LightUpdateQueue *queue) {
  arrfree(queue->items);
  memset(queue, 0, sizeof(*queue));
}

void LightQueue_Push(LightUpdateQueue *queue, LightNode node) {
  size_t write_index = ((size_t)queue->read_index + (size_t)queue->count) % queue->max_pending;
  queue->items[write_index] = node;
  if (queue->count == queue->max_pending) {
    queue->read_index = (queue->read_index + 1) % queue->max_pending;
  } else {
    queue->count++;
  }
}

bool LightQueue_Pop(LightUpdateQueue *queue, LightNode *out_node) {
  if (queue->count == 0) {
    return false;
  }

  *out_node = queue->items[queue->read_index];
  queue->read_index = (queue->read_index + 1) % queue->max_pending;
  queue->count--;
  return true;
}

static int column_light_at(int light, int y, int top, uint8_t block_id) {
  if (y >= top) {
    light = 15;
  } else {
    light -= Block_Opacity(block_id);
    if (light < 0) {
      light = 0;
    }
  }

  if (Block_IsOpaque(block_id)) {
    light = 0;
  }
  return light;
}

void Lighting_InitializeChunkSkylight(struct World *world, Chunk *chunk) {
  for (int lx = 0; lx < WORLD_CHUNK_SIZE_X; lx++) {
    for (int lz = 0; lz < WORLD_CHUNK_SIZE_Z; lz++) {
      int top = Chunk_GetHeight(chunk, lx, lz);
      int light = 15;

      for (int y = WORLD_MAX_HEIGHT - 1; y >= 0; y--) {
        light = column_light_at(light, y, top, Chunk_GetBlock(chunk, lx, y, lz));
        Chunk_SetSkyLight(chunk, lx, y, lz, light);

        LightNode node = {
            .x = chunk->cx * WORLD_CHUNK_SIZE_X + lx,
            .y = y,
            .z = chunk->cz * WORLD_CHUNK_SIZE_Z + lz,
        };
        LightQueue_Push(world->light_queue, node);
      }
    }
  }

  VoxelChunk_SetState(chunk, DIRTY);
}

void Lighting_RelightColumn(struct World *world, int x, int z) {
  int top = World_GetTopY(world, x, z);
  int light = 15;

  for (int y = WORLD_MAX_HEIGHT - 1; y >= 0; y--) {
    light = column_light_at(light, y, top, World_GetBlock(world, x, y, z));
    World_SetSkyLight(world, x, y, z, light);

    LightNode node = {.x = x, .y = y, .z = z};
    LightQueue_Push(world->light_queue, node);
  }
}

static const int QUEUE_AROUND_OFFSETS[7][3] = {
    {0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

void Lighting_QueueAround(struct World *world, int x, int y, int z) {
  for (int i = 0; i < 7; i++) {
    int ny = y + QUEUE_AROUND_OFFSETS[i][1];
    if (ny < 0 || ny >= WORLD_MAX_HEIGHT) {
      continue;
    }

    LightNode node = {
        .x = x + QUEUE_AROUND_OFFSETS[i][0],
        .y = ny,
        .z = z + QUEUE_AROUND_OFFSETS[i][2],
    };
    LightQueue_Push(world->light_queue, node);
  }
}

static const int NEIGHBOR_OFFSETS[6][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

static int get_max_neighbor_light(struct World *world, int x, int y, int z) {
  int max_light = 0;
  for (int i = 0; i < 6; i++) {
    int light = World_GetSkyLight(world, x + NEIGHBOR_OFFSETS[i][0], y + NEIGHBOR_OFFSETS[i][1],
                                  z + NEIGHBOR_OFFSETS[i][2]);
    if (light > max_light) {
      max_light = light;
    }
  }
  return max_light;
}

static int compute_target_skylight(struct World *world, int x, int y, int z) {
  if (y < 0 || y >= WORLD_MAX_HEIGHT) {
    return 0;
  }

  uint8_t block_id = World_GetBlock(world, x, y, z);
  if (Block_IsOpaque(block_id)) {
    return 0;
  }

  int source = (y >= World_GetTopY(world, x, z)) ? 15 : 0;

  int attenuation = Block_Opacity(block_id);
  if (attenuation <= 0) {
    attenuation = 1;
  }

  int propagated = get_max_neighbor_light(world, x, y, z) - attenuation;
  if (propagated < 0) {
    propagated = 0;
  }

  return (propagated > source) ? propagated : source;
}

int Lighting_Process(struct World *world, int budget) {
  LightUpdateQueue *queue = world->light_queue;
  int processed = 0;

  LightNode node;
  while (processed < budget && LightQueue_Pop(queue, &node)) {
    int current = World_GetSkyLight(world, node.x, node.y, node.z);
    int target = compute_target_skylight(world, node.x, node.y, node.z);

    if (current != target) {
      int cx = floor_div16(node.x);
      int cz = floor_div16(node.z);
      Chunk *chunk = World_GetChunk(world, cx, cz);

      World_SetSkyLight(world, node.x, node.y, node.z, target);

      /* Skip dirty marks while chunks are still in their initial lighting fill. */
      if (chunk && VoxelChunk_HasState(chunk, GENERATED) &&
          !VoxelChunk_HasState(chunk, LIGHTDIRTY)) {
        World_MarkChunkDirty(world, cx, cz);
      }

      for (int i = 0; i < 6; i++) {
        int ny = node.y + NEIGHBOR_OFFSETS[i][1];
        if (ny < 0 || ny >= WORLD_MAX_HEIGHT) {
          continue;
        }

        LightNode next = {
            .x = node.x + NEIGHBOR_OFFSETS[i][0],
            .y = ny,
            .z = node.z + NEIGHBOR_OFFSETS[i][2],
        };
        LightQueue_Push(queue, next);
      }
    }

    processed++;
  }

  return processed;
}
