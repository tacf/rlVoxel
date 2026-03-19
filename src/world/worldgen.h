#ifndef RLVOXEL_WORLDGEN_H
#define RLVOXEL_WORLDGEN_H

#include <stdbool.h>
#include <stdint.h>

#include "math/noise.h"
#include "math/random.h"
#include "world/chunk.h"

/**
 * WorldGen handles procedural terrain generation using noise functions.
 * It generates terrain heightmaps and places blocks based on biome data.
 */
typedef struct WorldGen {
  /** Seed for all random operations */
  int64_t seed;

  /** Random number generator */
  Random random;

  /** Noise samplers for terrain shaping */
  OctavePerlinNoiseSampler min_limit_perlin;
  OctavePerlinNoiseSampler max_limit_perlin;
  OctavePerlinNoiseSampler selector_noise;
  OctavePerlinNoiseSampler sand_gravel_noise;
  OctavePerlinNoiseSampler depth_noise;
  OctavePerlinNoiseSampler floating_island_scale;
  OctavePerlinNoiseSampler floating_island_noise;
  OctavePerlinNoiseSampler forest_noise;

  /** Noise samplers for biome calculation */
  OctaveSimplexNoiseSampler temperature_sampler;
  OctaveSimplexNoiseSampler downfall_sampler;
  OctaveSimplexNoiseSampler weirdness_sampler;

  /** Pre-allocated buffers for noise generation */
  double *height_map;
  double *sand_buffer;
  double *gravel_buffer;
  double *depth_buffer;
  double *selector_noise_buffer;
  double *min_limit_buffer;
  double *max_limit_buffer;
  double *scale_noise_buffer;
  double *depth_noise_buffer;

  /** Biome parameter maps */
  double *temperature_map;
  double *downfall_map;
  double *weirdness_map;
} WorldGen;

/**
 * Initializes the world generator with a seed.
 */
void WorldGen_Init(WorldGen *gen, int64_t seed);

/**
 * Frees all resources used by the world generator.
 */
void WorldGen_Shutdown(WorldGen *gen);

/**
 * Generates terrain for a single chunk.
 * Fills the chunk's block data with appropriate block types.
 */
void WorldGen_GenerateChunk(WorldGen *gen, Chunk *chunk);

/**
 * Gets the biome name at world coordinates.
 */
const char *WorldGen_GetBiomeName(const WorldGen *gen, int x, int z);

#endif /* RLVOXEL_WORLDGEN_H */
