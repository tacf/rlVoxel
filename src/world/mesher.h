#ifndef RLVOXEL_MESHER_H
#define RLVOXEL_MESHER_H

#include <raylib.h>
#include "world/chunk.h"

struct World;

void Mesher_RebuildChunk(struct World *world, Chunk *chunk, float ambient_multiplier);
void Mesher_Shutdown(void);

#endif
