#include "world/lighting.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "stb_ds.h"
#include "constants.h"
#include "world/blocks.h"
#include "world/chunk.h"
#include "world/world.h"

static int clamp_light(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 15) {
    return 15;
  }
  return value;
}

static void light_queue_compact(LightUpdateQueue *queue) {
  if (queue == NULL) {
    return;
  }

  ptrdiff_t len = arrlen(queue->items);
  if (queue->read_index <= 0) {
    return;
  }

  if (queue->read_index >= len) {
    arrsetlen(queue->items, 0);
    queue->read_index = 0;
    queue->count = 0;
    return;
  }

  if (queue->read_index > 1024 && queue->read_index * 2 >= len) {
    arrdeln(queue->items, 0, queue->read_index);
    queue->read_index = 0;
  }
}

void LightQueue_Init(LightUpdateQueue *queue, int capacity) {
  memset(queue, 0, sizeof(*queue));
  if (capacity < 1024) {
    capacity = 1024;
  }
  queue->max_pending = capacity;
  arrsetcap(queue->items, (size_t)capacity);
}

void LightQueue_Shutdown(LightUpdateQueue *queue) {
  arrfree(queue->items);
  memset(queue, 0, sizeof(*queue));
}

void LightQueue_Clear(LightUpdateQueue *queue) {
  arrsetlen(queue->items, 0);
  queue->read_index = 0;
  queue->count = 0;
}

bool LightQueue_Push(LightUpdateQueue *queue, LightNode node) {
  if (queue == NULL) {
    return false;
  }

  light_queue_compact(queue);

  ptrdiff_t pending = arrlen(queue->items) - queue->read_index;
  if (queue->max_pending > 0 && pending >= queue->max_pending) {
    /*
     * Keep accepting the newest light work by dropping the oldest pending node
     * when at capacity. This avoids a hard stall where all subsequent updates
     * are silently discarded forever.
     */
    if (pending > 0) {
      queue->read_index++;
      light_queue_compact(queue);
      pending = arrlen(queue->items) - queue->read_index;
    }
    if (queue->max_pending > 0 && pending >= queue->max_pending) {
      return false;
    }
  }

  arrput(queue->items, node);
  queue->count = (int)(arrlen(queue->items) - queue->read_index);
  return true;
}

bool LightQueue_Pop(LightUpdateQueue *queue, LightNode *out_node) {
  if (queue == NULL || out_node == NULL) {
    return false;
  }

  ptrdiff_t pending = arrlen(queue->items) - queue->read_index;
  if (pending <= 0) {
    queue->count = 0;
    return false;
  }

  *out_node = queue->items[queue->read_index++];
  light_queue_compact(queue);
  queue->count = (int)(arrlen(queue->items) - queue->read_index);
  return true;
}

static LightUpdateQueue *world_queue(struct World *world) {
  return (LightUpdateQueue *)world->light_queue_ptr;
}

static int column_sky_source_y(const Chunk *chunk, int lx, int lz) {
  return Chunk_GetHeight(chunk, lx, lz);
}

void Lighting_InitializeChunkSkylight(struct World *world, Chunk *chunk) {
  LightUpdateQueue *queue = world_queue(world);

  for (int lx = 0; lx < WORLD_CHUNK_SIZE_X; lx++) {
    for (int lz = 0; lz < WORLD_CHUNK_SIZE_Z; lz++) {
      int top = column_sky_source_y(chunk, lx, lz);
      int light = 15;

      for (int y = WORLD_MAX_HEIGHT - 1; y >= 0; y--) {
        uint8_t block_id = Chunk_GetBlock(chunk, lx, y, lz);

        if (y >= top) {
          light = 15;
        } else {
          int opacity = Block_Opacity(block_id);
          if (opacity > 0) {
            light -= opacity;
            if (light < 0) {
              light = 0;
            }
          }
        }

        if (Block_IsOpaque(block_id)) {
          light = 0;
        }

        Chunk_SetSkyLight(chunk, lx, y, lz, light);

        if (queue != NULL) {
          LightNode node = {
              .x = chunk->cx * WORLD_CHUNK_SIZE_X + lx,
              .y = y,
              .z = chunk->cz * WORLD_CHUNK_SIZE_Z + lz,
          };
          LightQueue_Push(queue, node);
        }
      }
    }
  }

  chunk->mesh_dirty = true;
}

void Lighting_RelightColumn(struct World *world, int x, int z) {
  LightUpdateQueue *queue = world_queue(world);
  int top = World_GetTopY(world, x, z);
  int light = 15;

  for (int y = WORLD_MAX_HEIGHT - 1; y >= 0; y--) {
    uint8_t block_id = World_GetBlock(world, x, y, z);

    if (y >= top) {
      light = 15;
    } else {
      int opacity = Block_Opacity(block_id);
      if (opacity > 0) {
        light -= opacity;
        if (light < 0) {
          light = 0;
        }
      }
    }

    if (Block_IsOpaque(block_id)) {
      light = 0;
    }

    World_SetSkyLight(world, x, y, z, light);

    if (queue != NULL) {
      LightNode node = {
          .x = x,
          .y = y,
          .z = z,
      };
      LightQueue_Push(queue, node);
    }
  }
}

static const int QUEUE_AROUND_OFFSETS[7][3] = {
    {0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

void Lighting_QueueAround(struct World *world, int x, int y, int z) {
  LightUpdateQueue *queue = world_queue(world);

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
    LightQueue_Push(queue, node);
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

  int top = World_GetTopY(world, x, z);
  int source = (y >= top) ? 15 : 0;

  int max_neighbor = get_max_neighbor_light(world, x, y, z);

  int attenuation = Block_Opacity(block_id);
  if (attenuation <= 0) {
    attenuation = 1;
  }

  int propagated = max_neighbor - attenuation;
  if (propagated < 0) {
    propagated = 0;
  }

  int target = source;
  if (propagated > target) {
    target = propagated;
  }

  return clamp_light(target);
}

int Lighting_Process(struct World *world, int budget) {
  LightUpdateQueue *queue = world_queue(world);
  if (queue == NULL) {
    return 0;
  }

  int processed = 0;

  LightNode node;
  while (processed < budget && LightQueue_Pop(queue, &node)) {
    int current = World_GetSkyLight(world, node.x, node.y, node.z);
    int target = compute_target_skylight(world, node.x, node.y, node.z);

    if (current != target) {
      int cx = node.x >> 4;
      int cz = node.z >> 4;
      Chunk *chunk = World_GetChunk(world, cx, cz);

      World_SetSkyLight(world, node.x, node.y, node.z, target);

      /* Skip dirty marks while chunks are still in their initial lighting fill. */
      if (chunk && chunk->generated && !chunk->lighting_dirty) {
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
