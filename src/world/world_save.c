#include "world/world_save.h"
#include "raylib.h"
#include "voxel/chunk.h"

#include <asm-generic/errno-base.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "constants.h"
#include "game/player.h"

#define SAVE_VERSION 1u
#define CHUNK_BYTES                                                                                \
  (WORLD_CHUNK_VOLUME + WORLD_CHUNK_LIGHT_BYTES + WORLD_CHUNK_SIZE_X * WORLD_CHUNK_SIZE_Z)

static bool fail(char *error, const char *message) {
  if (error)
    snprintf(error, WORLD_SAVE_ERROR_SIZE, "%s", message);
  return false;
}

static bool path_join(char *out, const char *directory, const char *file, char *error) {
  int n = snprintf(out, WORLD_SAVE_PATH_SIZE, "%s/%s", directory, file);
  return n >= 0 && n < WORLD_SAVE_PATH_SIZE ? true : fail(error, "Save path is too long.");
}

static void put_u32(uint8_t **p, uint32_t value) {
  for (int i = 0; i < 4; i++)
    *(*p)++ = (uint8_t)(value >> (i * 8));
}

static uint32_t get_u32(const uint8_t **p) {
  uint32_t value = 0;
  for (int i = 0; i < 4; i++)
    value |= (uint32_t)*(*p)++ << (i * 8);
  return value;
}

static uint32_t checksum(const uint8_t *bytes, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; i++)
    hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}

/* The previous file survives failed writes. Each file is independently atomic. */
static bool write_file(const char *path, const void *bytes, size_t size, char *error) {
  char temp[WORLD_SAVE_PATH_SIZE + 8];
  snprintf(temp, sizeof(temp), "%s.tmp", path);
  FILE *file = fopen(temp, "wb");
  if (!file)
    return fail(error, "Cannot open save file for writing. Check folder permissions.");
  bool ok = fwrite(bytes, 1, size, file) == size && fflush(file) == 0;
#ifndef _WIN32
  if (ok)
    ok = fsync(fileno(file)) == 0;
#endif
  if (fclose(file) != 0)
    ok = false;
  if (ok)
    ok = rename(temp, path) == 0;
  if (!ok) {
    remove(temp);
    return fail(error, "Could not commit save file. Check disk space and folder permissions.");
  }
  return true;
}

static WorldSaveLoadResult read_chunk_file(const char *path, uint8_t *bytes, size_t size,
                                           char *error) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    if (errno == ENOENT)
      return WORLD_SAVE_MISSING;
    fail(error, "Cannot read save file. Check folder permissions.");
    return WORLD_SAVE_ERROR;
  }
  bool ok = fread(bytes, 1, size, file) == size;
  if (ok)
    ok = fgetc(file) == EOF && !ferror(file);
  fclose(file);
  const uint8_t *p = bytes + size - 4;
  if (!ok || get_u32(&p) != checksum(bytes, size - 4)) {
    fail(error, "Save file is truncated or corrupt. It has not been overwritten.");
    return WORLD_SAVE_ERROR;
  }
  return WORLD_SAVE_LOADED;
}

static bool valid_name(const char *name) {
  size_t length = strlen(name);
  if (!length || length >= WORLD_SAVE_NAME_SIZE)
    return false;
  bool nonspace = false;
  for (size_t i = 0; i < length; i++) {
    if ((unsigned char)name[i] < 32 || (unsigned char)name[i] == 127)
      return false;
    if (name[i] != ' ')
      nonspace = true;
  }
  return nonspace;
}

static bool valid_info(const WorldSaveInfo *info) {
  return valid_name(info->name) && isfinite(info->world_time) && isfinite(info->position.x) &&
         isfinite(info->position.y) && isfinite(info->position.z) &&
         fabsf(info->position.x) <= 10000000 && fabsf(info->position.y) <= 10000000 &&
         fabsf(info->position.z) <= 10000000 && isfinite(info->yaw) && isfinite(info->pitch) &&
         fabsf(info->pitch) <= PLAYER_PITCH_LIMIT &&
         info->gameplay_mode >= GAMEPLAY_MODE_CREATIVE &&
         info->gameplay_mode <= GAMEPLAY_MODE_SURVIVAL && info->hotbar_index >= 0 &&
         info->hotbar_index < PLAYER_HOTBAR_SLOTS;
}

bool WorldSave_WriteInfo(const WorldSaveInfo *info, char *error) {
  if (!valid_name(info->name))
    return fail(error, "Enter a world name (1 to 63 characters).");
  if (!valid_info(info))
    return fail(error, "World metadata contains invalid values.");
  char path[WORLD_SAVE_PATH_SIZE];
  if (!path_join(path, info->directory, "world.meta", error))
    return false;
  char text[768];
  int length = snprintf(text, sizeof(text),
                        "RLVOXEL_WORLD %u\n%s\n"
                        "seed: %" PRId64 "\nsaved: %" PRIu64 "\ntime: %.9g\n"
                        "player: %d %.9g %.9g %.9g %.9g %.9g\n"
                        "mode: %d %d %d %u\n",
                        SAVE_VERSION, info->name, info->seed, info->last_saved, info->world_time,
                        info->has_player, info->position.x, info->position.y, info->position.z,
                        info->yaw, info->pitch, info->gameplay_mode, info->fly_enabled,
                        info->hotbar_index, info->selected_block);
  if (length < 0 || (size_t)length >= sizeof(text))
    return fail(error, "World metadata is too long.");
  return write_file(path, text, (size_t)length, error);
}

bool WorldSave_ReadInfo(const char *directory, WorldSaveInfo *info, char *error) {
  char path[WORLD_SAVE_PATH_SIZE];
  if (!path_join(path, directory, "world.meta", error))
    return false;
  FILE *file = fopen(path, "rb");
  if (!file)
    return fail(error, "Cannot read world metadata.");
  char header[64], name[WORLD_SAVE_NAME_SIZE + 2];
  char expected_header[64];
  snprintf(expected_header, sizeof(expected_header), "RLVOXEL_WORLD %u\n", SAVE_VERSION);
  bool valid = fgets(header, sizeof(header), file) && !strcmp(header, expected_header) &&
               fgets(name, sizeof(name), file);
  if (!valid) {
    fclose(file);
    return fail(error, "World metadata is missing, truncated, or uses an unsupported format.");
  }
  size_t name_length = strcspn(name, "\r\n");
  valid = name[name_length] != 0 && name_length < WORLD_SAVE_NAME_SIZE;
  name[name_length] = 0;
  WorldSaveInfo loaded = {0};
  snprintf(loaded.directory, sizeof(loaded.directory), "%s", directory);
  if (valid)
    memcpy(loaded.name, name, name_length + 1);
  int has_player = 0, fly = 0;
  unsigned int selected = 0;
  int fields = fscanf(file,
                      "seed: %" SCNd64 "\nsaved: %" SCNu64 "\ntime: %f\n"
                      "player: %d %f %f %f %f %f\nmode: %d %d %d %u\n",
                      &loaded.seed, &loaded.last_saved, &loaded.world_time, &has_player,
                      &loaded.position.x, &loaded.position.y, &loaded.position.z, &loaded.yaw,
                      &loaded.pitch, &loaded.gameplay_mode, &fly, &loaded.hotbar_index, &selected);
  valid = valid && fields == 13 && fgetc(file) == EOF && !ferror(file);
  fclose(file);
  loaded.has_player = has_player != 0;
  loaded.fly_enabled = fly != 0;
  loaded.selected_block = (uint8_t)selected;
  if (!valid || !valid_info(&loaded) || fly < 0 || fly > 1 || has_player < 0 || has_player > 1 ||
      selected > UINT8_MAX)
    return fail(error, "World metadata contains invalid values.");
  *info = loaded;
  return true;
}

bool WorldSave_Create(const char *root, const char *name, int64_t seed, int mode,
                      WorldSaveInfo *info, char *error) {
  if (!valid_name(name))
    return fail(error, "Enter a world name (1 to 63 characters).");
  if (!DirectoryExists(root) && MakeDirectory(root) != 0)
    return fail(error, "Cannot create the worlds folder.");
  WorldSaveInfo created = {.seed = seed, .world_time = 6000, .gameplay_mode = mode};
  snprintf(created.name, sizeof(created.name), "%s", name);
  for (unsigned int i = 0; i < 10000; i++) {
    char id[64];
    snprintf(id, sizeof(id), "world-%" PRIu64 "-%u", (uint64_t)time(NULL), i);
    if (!path_join(created.directory, root, id, error))
      return false;
    if (DirectoryExists(created.directory))
      continue;
    if (MakeDirectory(created.directory) != 0)
      return fail(error, "Cannot create world folder.");
    char chunks[WORLD_SAVE_PATH_SIZE];
    if (!path_join(chunks, created.directory, "chunks", error) || MakeDirectory(chunks) != 0)
      return fail(error, "Cannot create chunk folder.");
    if (!WorldSave_WriteInfo(&created, error))
      return false;
    *info = created;
    return true;
  }
  return fail(error, "Could not allocate a unique world folder.");
}

bool WorldSave_Rename(WorldSaveInfo *info, const char *name, char *error) {
  if (!valid_name(name))
    return fail(error, "Enter a world name (1 to 63 characters).");
  WorldSaveInfo renamed = *info;
  snprintf(renamed.name, sizeof(renamed.name), "%s", name);
  if (!WorldSave_WriteInfo(&renamed, error))
    return false;
  *info = renamed;
  return true;
}

bool WorldSave_Archive(const char *root, const WorldSaveInfo *info, char *error) {
  char trash[WORLD_SAVE_PATH_SIZE], target[WORLD_SAVE_PATH_SIZE];
  if (!path_join(trash, root, ".trash", error))
    return false;
  if (!DirectoryExists(trash) && MakeDirectory(trash) != 0)
    return fail(error, "Cannot create the world archive folder.");
  const char *id = GetFileName(info->directory);
  for (unsigned int i = 0; i < 10000; i++) {
    char archived[96];
    snprintf(archived, sizeof(archived), "%s-%u", id, i);
    if (!path_join(target, trash, archived, error))
      return false;
    if (DirectoryExists(target))
      continue;
    return rename(info->directory, target) == 0 ? true
                                                : fail(error, "Could not archive this world.");
  }
  return fail(error, "Could not allocate an archive folder.");
}

static bool chunk_path(char *path, const char *directory, const VoxelChunk *chunk, char *error) {
  char filename[80];
  snprintf(filename, sizeof(filename), "chunks/%d_%d.bin", chunk->cx, chunk->cz);
  return path_join(path, directory, filename, error);
}

bool WorldSave_WriteChunk(const char *directory, const VoxelChunk *chunk, char *error) {
  char path[WORLD_SAVE_PATH_SIZE];
  if (!chunk_path(path, directory, chunk, error))
    return false;
  uint8_t *bytes = malloc(16 + CHUNK_BYTES + 4);
  if (!bytes)
    return fail(error, "Not enough memory to save a chunk.");
  memcpy(bytes, "RLVC", 4);
  uint8_t *p = bytes + 4;
  put_u32(&p, SAVE_VERSION);
  put_u32(&p, (uint32_t)chunk->cx);
  put_u32(&p, (uint32_t)chunk->cz);
  memcpy(p, chunk->blocks, WORLD_CHUNK_VOLUME);
  p += WORLD_CHUNK_VOLUME;
  memcpy(p, chunk->skylight, WORLD_CHUNK_LIGHT_BYTES);
  p += WORLD_CHUNK_LIGHT_BYTES;
  memcpy(p, chunk->heightmap, sizeof(chunk->heightmap));
  p = bytes + 16 + CHUNK_BYTES;
  put_u32(&p, checksum(bytes, 16 + CHUNK_BYTES));
  bool ok = write_file(path, bytes, 16 + CHUNK_BYTES + 4, error);
  free(bytes);
  return ok;
}

WorldSaveLoadResult WorldSave_ReadChunk(const char *directory, VoxelChunk *chunk, char *error) {
  char path[WORLD_SAVE_PATH_SIZE];
  if (!chunk_path(path, directory, chunk, error))
    return WORLD_SAVE_ERROR;
  uint8_t *bytes = malloc(16 + CHUNK_BYTES + 4);
  if (!bytes) {
    fail(error, "Not enough memory to load a chunk.");
    return WORLD_SAVE_ERROR;
  }
  WorldSaveLoadResult result = read_chunk_file(path, bytes, 16 + CHUNK_BYTES + 4, error);
  if (result == WORLD_SAVE_LOADED) {
    const uint8_t *p = bytes + 4;
    bool valid = !memcmp(bytes, "RLVC", 4) && get_u32(&p) == SAVE_VERSION;
    if (valid)
      valid = get_u32(&p) == (uint32_t)chunk->cx;
    if (valid)
      valid = get_u32(&p) == (uint32_t)chunk->cz;
    if (!valid) {
      fail(error, "Chunk header is invalid or uses an unsupported format.");
      result = WORLD_SAVE_ERROR;
    } else {
      memcpy(chunk->blocks, p, WORLD_CHUNK_VOLUME);
      p += WORLD_CHUNK_VOLUME;
      memcpy(chunk->skylight, p, WORLD_CHUNK_LIGHT_BYTES);
      p += WORLD_CHUNK_LIGHT_BYTES;
      memcpy(chunk->heightmap, p, sizeof(chunk->heightmap));
    }
  }
  free(bytes);
  return result;
}
