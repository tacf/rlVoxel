#ifndef RLVOXEL_LIGHTING_H
#define RLVOXEL_LIGHTING_H

#include <stdbool.h>
#include <stddef.h>

#include "world/chunk.h"

typedef struct LightNode {
  int x;
  int y;
  int z;
} LightNode;

typedef struct LightUpdateQueue {
  /* Fixed ring buffer. At capacity, new work replaces the oldest pending node. 
   * It's better than before but we still might lose work so we need to circle back
   * to this.. get it? ring.. circle back?
   */
  LightNode *items;
  ptrdiff_t read_index;
  int count;
  int max_pending;
} LightUpdateQueue;

struct World;

void LightQueue_Init(LightUpdateQueue *queue, int capacity);
void LightQueue_Shutdown(LightUpdateQueue *queue);
void LightQueue_Push(LightUpdateQueue *queue, LightNode node);
bool LightQueue_Pop(LightUpdateQueue *queue, LightNode *out_node);

void Lighting_InitializeChunkSkylight(struct World *world, Chunk *chunk);
void Lighting_RelightColumn(struct World *world, int x, int z);
void Lighting_QueueAround(struct World *world, int x, int y, int z);
int Lighting_Process(struct World *world, int budget);

#endif
