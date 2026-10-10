#ifndef RLVOXEL_WORLD_SAVE_H
#define RLVOXEL_WORLD_SAVE_H

#include <stdbool.h>
#include <stdint.h>
#include <raylib.h>
#include "voxel/chunk.h"

#define WORLD_SAVE_PATH_SIZE 512
#define WORLD_SAVE_NAME_SIZE 64
#define WORLD_SAVE_ERROR_SIZE 256

typedef struct WorldSaveInfo {
  char directory[WORLD_SAVE_PATH_SIZE];
  char name[WORLD_SAVE_NAME_SIZE];
  int64_t seed;
  uint64_t last_saved;
  float world_time;
  bool has_player;
  Vector3 position;
  float yaw;
  float pitch;
  int gameplay_mode;
  bool fly_enabled;
  int hotbar_index;
  uint8_t selected_block;
} WorldSaveInfo;

typedef enum WorldSaveLoadResult {
  WORLD_SAVE_ERROR = -1,
  WORLD_SAVE_MISSING = 0,
  WORLD_SAVE_LOADED = 1,
} WorldSaveLoadResult;

bool WorldSave_Create(const char *root, const char *name, int64_t seed, int mode,
                      WorldSaveInfo *info, char *error);
bool WorldSave_ReadInfo(const char *directory, WorldSaveInfo *info, char *error);
bool WorldSave_WriteInfo(const WorldSaveInfo *info, char *error);
bool WorldSave_Rename(WorldSaveInfo *info, const char *name, char *error);
/* Archive into root/.trash so removal from the browser is recoverable. */
bool WorldSave_Archive(const char *root, const WorldSaveInfo *info, char *error);
WorldSaveLoadResult WorldSave_ReadChunk(const char *directory, VoxelChunk *chunk, char *error);
bool WorldSave_WriteChunk(const char *directory, const VoxelChunk *chunk, char *error);

#endif
