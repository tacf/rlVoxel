#include "world/worldgen.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "math/noise.h"
#include "math/random.h"
#include "world/blocks.h"
#include "world/chunk.h"

typedef enum BiomeType {
  BIOME_RAINFOREST = 0,
  BIOME_SWAMPLAND,
  BIOME_SEASONAL_FOREST,
  BIOME_FOREST,
  BIOME_SAVANNA,
  BIOME_SHRUBLAND,
  BIOME_TAIGA,
  BIOME_DESERT,
  BIOME_PLAINS,
  BIOME_ICE_DESERT,
  BIOME_TUNDRA,
  BIOME_COUNT
} BiomeType;

typedef struct BiomeDef {
  uint8_t top_block;
  uint8_t soil_block;
} BiomeDef;

static const BiomeDef BIOME_TABLE[BIOME_COUNT] = {
    [BIOME_RAINFOREST] = {.top_block = BLOCK_GRASS, .soil_block = BLOCK_DIRT},
    [BIOME_SWAMPLAND] = {.top_block = BLOCK_GRASS, .soil_block = BLOCK_DIRT},
    [BIOME_SEASONAL_FOREST] = {.top_block = BLOCK_GRASS, .soil_block = BLOCK_DIRT},
    [BIOME_FOREST] = {.top_block = BLOCK_GRASS, .soil_block = BLOCK_DIRT},
    [BIOME_SAVANNA] = {.top_block = BLOCK_GRASS, .soil_block = BLOCK_DIRT},
    [BIOME_SHRUBLAND] = {.top_block = BLOCK_GRASS, .soil_block = BLOCK_DIRT},
    [BIOME_TAIGA] = {.top_block = BLOCK_GRASS, .soil_block = BLOCK_DIRT},
    [BIOME_DESERT] = {.top_block = BLOCK_SAND, .soil_block = BLOCK_SAND},
    [BIOME_PLAINS] = {.top_block = BLOCK_GRASS, .soil_block = BLOCK_DIRT},
    [BIOME_ICE_DESERT] = {.top_block = BLOCK_SAND, .soil_block = BLOCK_SAND},
    [BIOME_TUNDRA] = {.top_block = BLOCK_GRASS, .soil_block = BLOCK_DIRT},
};

typedef enum TreeType { TREE_OAK = 0, TREE_BIRCH, TREE_SPRUCE, TREE_PINE, TREE_LARGE_OAK } TreeType;

static int chunk_world_x_min(const Chunk *chunk) { return chunk->cx * WORLD_CHUNK_SIZE_X; }

static int chunk_world_z_min(const Chunk *chunk) { return chunk->cz * WORLD_CHUNK_SIZE_Z; }

static bool chunk_world_to_local(const Chunk *chunk, int wx, int wz, int *out_lx, int *out_lz) {
  int lx = wx - chunk_world_x_min(chunk);
  int lz = wz - chunk_world_z_min(chunk);
  if (lx < 0 || lx >= WORLD_CHUNK_SIZE_X || lz < 0 || lz >= WORLD_CHUNK_SIZE_Z) {
    return false;
  }
  if (out_lx != NULL) {
    *out_lx = lx;
  }
  if (out_lz != NULL) {
    *out_lz = lz;
  }
  return true;
}

static uint8_t chunk_get_block_world(const Chunk *chunk, int wx, int y, int wz) {
  if (y < 0 || y >= WORLD_MAX_HEIGHT) {
    return BLOCK_AIR;
  }

  int lx;
  int lz;
  if (!chunk_world_to_local(chunk, wx, wz, &lx, &lz)) {
    return BLOCK_AIR;
  }

  return Chunk_GetBlock(chunk, lx, y, lz);
}

static void chunk_set_block_world(Chunk *chunk, int wx, int y, int wz, uint8_t block_id) {
  if (y < 0 || y >= WORLD_MAX_HEIGHT) {
    return;
  }

  int lx;
  int lz;
  if (!chunk_world_to_local(chunk, wx, wz, &lx, &lz)) {
    return;
  }

  Chunk_SetBlock(chunk, lx, y, lz, block_id);
}

static int chunk_get_top_y_world(const Chunk *chunk, int wx, int wz) {
  int lx;
  int lz;
  if (!chunk_world_to_local(chunk, wx, wz, &lx, &lz)) {
    return 0;
  }
  return Chunk_GetHeight(chunk, lx, lz);
}

static bool can_grow_tall_grass(const Chunk *chunk, int wx, int y, int wz) {
  if (y <= 0 || y >= WORLD_MAX_HEIGHT) {
    return false;
  }

  if (chunk_get_block_world(chunk, wx, y, wz) != BLOCK_AIR) {
    return false;
  }

  uint8_t below = chunk_get_block_world(chunk, wx, y - 1, wz);
  return below == BLOCK_GRASS || below == BLOCK_DIRT;
}

static bool is_tree_replaceable(uint8_t block_id) {
  return block_id == BLOCK_AIR || block_id == BLOCK_LEAVES;
}

static void set_tree_leaf(Chunk *chunk, int wx, int y, int wz) {
  if (Block_IsOpaque(chunk_get_block_world(chunk, wx, y, wz))) {
    return;
  }
  chunk_set_block_world(chunk, wx, y, wz, BLOCK_LEAVES);
}

static void set_tree_log(Chunk *chunk, int wx, int y, int wz) {
  uint8_t existing = chunk_get_block_world(chunk, wx, y, wz);
  if (existing == BLOCK_AIR || existing == BLOCK_LEAVES) {
    chunk_set_block_world(chunk, wx, y, wz, BLOCK_LOG);
  }
}

typedef struct {
  int min_height;
  int max_height;
  int trunk_no_leaves;
  int max_leaf_radius;
  int canopy_layers;
  int leaf_spread_y;
} TreeConfig;

static bool check_tree_placement(Chunk *chunk, int x, int y, int z, int tree_height,
                                 int check_radius) {
  if (!(y >= 1 && y + tree_height + 1 <= WORLD_MAX_HEIGHT)) {
    return false;
  }

  for (int cy = y; cy <= y + 1 + tree_height; cy++) {
    int radius = (cy == y) ? 0 : ((cy >= y + tree_height - 1) ? 2 : check_radius);

    for (int cx = x - radius; cx <= x + radius; cx++) {
      for (int cz = z - radius; cz <= z + radius; cz++) {
        if (cy < 0 || cy >= WORLD_MAX_HEIGHT) {
          return false;
        }
        if (!is_tree_replaceable(chunk_get_block_world(chunk, cx, cy, cz))) {
          return false;
        }
      }
    }
  }
  return true;
}

static void build_tree_trunk(Chunk *chunk, int x, int y, int z, int height) {
  for (int trunk_y = 0; trunk_y < height; trunk_y++) {
    set_tree_log(chunk, x, y + trunk_y, z);
  }
}

static void build_oak_like_leaves(Chunk *chunk, Random *random, int x, int y, int z,
                                  int tree_height) {
  for (int leaf_y = y - 3 + tree_height; leaf_y <= y + tree_height; leaf_y++) {
    int relative_y = leaf_y - (y + tree_height);
    int leaf_radius = 1 - relative_y / 2;

    for (int leaf_x = x - leaf_radius; leaf_x <= x + leaf_radius; leaf_x++) {
      int offset_x = leaf_x - x;

      for (int leaf_z = z - leaf_radius; leaf_z <= z + leaf_radius; leaf_z++) {
        int offset_z = leaf_z - z;
        bool skip_corner = (abs(offset_x) == leaf_radius && abs(offset_z) == leaf_radius &&
                            (Random_NextIntBounded(random, 2) == 0 || relative_y == 0));
        if (!skip_corner) {
          set_tree_leaf(chunk, leaf_x, leaf_y, leaf_z);
        }
      }
    }
  }
}

static void build_cone_leaves(Chunk *chunk, Random *random, int x, int y, int z, int total_height,
                              int top_trunk_no_leaves, int max_leaf_radius) {
  int leaf_start_offset = total_height - top_trunk_no_leaves;
  int current_radius = Random_NextIntBounded(random, 2);
  int radius_target = 1;
  int radius_step = 0;

  for (int h = 0; h <= leaf_start_offset; h++) {
    int leaf_y = y + total_height - h;

    for (int cx = x - current_radius; cx <= x + current_radius; cx++) {
      int offset_x = cx - x;

      for (int cz = z - current_radius; cz <= z + current_radius; cz++) {
        int offset_z = cz - z;
        if ((abs(offset_x) != current_radius || abs(offset_z) != current_radius ||
             current_radius <= 0)) {
          set_tree_leaf(chunk, cx, leaf_y, cz);
        }
      }
    }

    if (current_radius >= radius_target) {
      current_radius = radius_step;
      radius_step = 1;
      radius_target++;
      if (radius_target > max_leaf_radius) {
        radius_target = max_leaf_radius;
      }
    } else {
      current_radius++;
    }
  }
}

static void build_pyramid_leaves(Chunk *chunk, int x, int y, int z, int tree_height,
                                 int trunk_no_leaves, int max_leaf_radius) {
  int current_leaf_radius = 0;

  for (int cy = y + tree_height; cy >= y + trunk_no_leaves; cy--) {
    for (int cx = x - current_leaf_radius; cx <= x + current_leaf_radius; cx++) {
      int offset_x = cx - x;

      for (int cz = z - current_leaf_radius; cz <= z + current_leaf_radius; cz++) {
        int offset_z = cz - z;
        if ((abs(offset_x) != current_leaf_radius || abs(offset_z) != current_leaf_radius ||
             current_leaf_radius <= 0)) {
          set_tree_leaf(chunk, cx, cy, cz);
        }
      }
    }

    if (current_leaf_radius >= 1 && cy == y + trunk_no_leaves + 1) {
      current_leaf_radius--;
    } else if (current_leaf_radius < max_leaf_radius) {
      current_leaf_radius++;
    }
  }
}

static void build_large_oak_leaves(Chunk *chunk, Random *random, int x, int y, int z,
                                   int tree_height) {
  int canopy_base = y + tree_height - 5;
  for (int leaf_y = canopy_base; leaf_y <= y + tree_height; leaf_y++) {
    int rel = leaf_y - (y + tree_height);
    int leaf_radius = (rel >= -1) ? 2 : 3;

    for (int leaf_x = x - leaf_radius; leaf_x <= x + leaf_radius; leaf_x++) {
      for (int leaf_z = z - leaf_radius; leaf_z <= z + leaf_radius; leaf_z++) {
        int dx = leaf_x - x;
        int dz = leaf_z - z;
        if (abs(dx) == leaf_radius && abs(dz) == leaf_radius &&
            Random_NextIntBounded(random, 2) == 0) {
          continue;
        }
        set_tree_leaf(chunk, leaf_x, leaf_y, leaf_z);
      }
    }
  }
}

static BiomeType locate_biome(double temperature, double downfall) {
  downfall *= temperature;

  if (temperature < 0.1) {
    return BIOME_TUNDRA;
  }

  if (downfall < 0.2) {
    if (temperature < 0.5) {
      return BIOME_TUNDRA;
    }
    return (temperature < 0.95) ? BIOME_SAVANNA : BIOME_DESERT;
  }

  if (downfall > 0.5 && temperature < 0.7) {
    return BIOME_SWAMPLAND;
  }

  if (temperature < 0.5) {
    return BIOME_TAIGA;
  }

  if (temperature < 0.97) {
    return (downfall < 0.35) ? BIOME_SHRUBLAND : BIOME_FOREST;
  }

  if (downfall < 0.45) {
    return BIOME_PLAINS;
  }

  return (downfall < 0.9) ? BIOME_SEASONAL_FOREST : BIOME_RAINFOREST;
}

static double clamp_unit(double value) {
  if (value < 0.0) {
    return 0.0;
  }
  if (value > 1.0) {
    return 1.0;
  }
  return value;
}

static void normalize_biome_inputs(double raw_temperature, double raw_downfall,
                                   double raw_weirdness, double *temperature, double *downfall) {
  double weirdness = raw_weirdness * 1.1 + 0.5;

  double temp_weight = 0.01;
  double temp_one_minus_weight = 1.0 - temp_weight;
  double normalized_temperature =
      (raw_temperature * 0.15 + 0.7) * temp_one_minus_weight + weirdness * temp_weight;

  double downfall_weight = 0.002;
  double downfall_one_minus_weight = 1.0 - downfall_weight;
  double normalized_downfall =
      (raw_downfall * 0.15 + 0.5) * downfall_one_minus_weight + weirdness * downfall_weight;

  normalized_temperature = 1.0 - (1.0 - normalized_temperature) * (1.0 - normalized_temperature);

  if (temperature != NULL) {
    *temperature = clamp_unit(normalized_temperature);
  }
  if (downfall != NULL) {
    *downfall = clamp_unit(normalized_downfall);
  }
}

static void sample_biomes(WorldGen *gen, int x, int z, int width, int depth, BiomeType *biomes,
                          double *temperature_map_out) {
  size_t size = (size_t)width * (size_t)depth;

  gen->temperature_map =
      OctaveSimplex_Sample(&gen->temperature_sampler, gen->temperature_map, (double)x, (double)z,
                           width, depth, 0.025, 0.025, 0.25, 0.5);

  gen->downfall_map = OctaveSimplex_Sample(&gen->downfall_sampler, gen->downfall_map, (double)x,
                                           (double)z, width, depth, 0.05, 0.05, 1.0 / 3.0, 0.5);

  gen->weirdness_map =
      OctaveSimplex_Sample(&gen->weirdness_sampler, gen->weirdness_map, (double)x, (double)z, width,
                           depth, 0.25, 0.25, 0.5882352941176471, 0.5);

  if (gen->temperature_map == NULL || gen->downfall_map == NULL || gen->weirdness_map == NULL) {
    return;
  }

  for (size_t i = 0; i < size; i++) {
    double temperature = 0.5;
    double downfall = 0.5;
    normalize_biome_inputs(gen->temperature_map[i], gen->downfall_map[i], gen->weirdness_map[i],
                           &temperature, &downfall);

    gen->temperature_map[i] = temperature;
    gen->downfall_map[i] = downfall;

    if (temperature_map_out != NULL) {
      temperature_map_out[i] = temperature;
    }

    biomes[i] = locate_biome(temperature, downfall);
  }
}

static BiomeType sample_biome_at(WorldGen *gen, int x, int z) {
  BiomeType biome = BIOME_PLAINS;
  sample_biomes(gen, x, z, 1, 1, &biome, NULL);
  return biome;
}

static bool generate_oak_tree(Chunk *chunk, Random *random, int x, int y, int z) {
  int tree_height = Random_NextIntBounded(random, 3) + 4;
  if (!check_tree_placement(chunk, x, y, z, tree_height, 1)) {
    return false;
  }

  uint8_t soil = chunk_get_block_world(chunk, x, y - 1, z);
  if (!(soil == BLOCK_GRASS || soil == BLOCK_DIRT) || y >= WORLD_MAX_HEIGHT - tree_height - 1) {
    return false;
  }

  chunk_set_block_world(chunk, x, y - 1, z, BLOCK_DIRT);
  build_tree_trunk(chunk, x, y, z, tree_height);
  build_oak_like_leaves(chunk, random, x, y, z, tree_height);
  return true;
}

static bool generate_birch_tree(Chunk *chunk, Random *random, int x, int y, int z) {
  int tree_height = Random_NextIntBounded(random, 3) + 5;
  if (!check_tree_placement(chunk, x, y, z, tree_height, 1)) {
    return false;
  }

  uint8_t soil = chunk_get_block_world(chunk, x, y - 1, z);
  if (!(soil == BLOCK_GRASS || soil == BLOCK_DIRT) || y >= WORLD_MAX_HEIGHT - tree_height - 1) {
    return false;
  }

  chunk_set_block_world(chunk, x, y - 1, z, BLOCK_DIRT);
  build_tree_trunk(chunk, x, y, z, tree_height);
  build_oak_like_leaves(chunk, random, x, y, z, tree_height);
  return true;
}

static bool generate_spruce_tree(Chunk *chunk, Random *random, int x, int y, int z) {
  int total_height = Random_NextIntBounded(random, 4) + 6;
  int top_trunk_no_leaves = 1 + Random_NextIntBounded(random, 2);
  int max_leaf_radius = 2 + Random_NextIntBounded(random, 2);

  if (!check_tree_placement(chunk, x, y, z, total_height, max_leaf_radius)) {
    return false;
  }

  uint8_t ground = chunk_get_block_world(chunk, x, y - 1, z);
  if (!((ground == BLOCK_GRASS || ground == BLOCK_DIRT) &&
        y < WORLD_MAX_HEIGHT - total_height - 1)) {
    return false;
  }

  chunk_set_block_world(chunk, x, y - 1, z, BLOCK_DIRT);

  int trunk_variability = Random_NextIntBounded(random, 3);
  build_tree_trunk(chunk, x, y, z, total_height - trunk_variability);
  build_cone_leaves(chunk, random, x, y, z, total_height, top_trunk_no_leaves, max_leaf_radius);
  return true;
}

static bool generate_pine_tree(Chunk *chunk, Random *random, int x, int y, int z) {
  int tree_height = Random_NextIntBounded(random, 5) + 7;
  int trunk_no_leaves = tree_height - Random_NextIntBounded(random, 2) - 3;
  int canopy_height = tree_height - trunk_no_leaves;
  int max_leaf_radius = 1 + Random_NextIntBounded(random, canopy_height + 1);

  if (!check_tree_placement(chunk, x, y, z, tree_height, max_leaf_radius)) {
    return false;
  }

  uint8_t ground = chunk_get_block_world(chunk, x, y - 1, z);
  if (!((ground == BLOCK_GRASS || ground == BLOCK_DIRT) &&
        y < WORLD_MAX_HEIGHT - tree_height - 1)) {
    return false;
  }

  chunk_set_block_world(chunk, x, y - 1, z, BLOCK_DIRT);
  build_tree_trunk(chunk, x, y, z, tree_height - 1);
  build_pyramid_leaves(chunk, x, y, z, tree_height, trunk_no_leaves, max_leaf_radius);
  return true;
}

static bool generate_large_oak_tree(Chunk *chunk, Random *random, int x, int y, int z) {
  int tree_height = 5 + Random_NextIntBounded(random, 12);
  if (!check_tree_placement(chunk, x, y, z, tree_height, 3)) {
    return false;
  }

  uint8_t soil = chunk_get_block_world(chunk, x, y - 1, z);
  if (!(soil == BLOCK_GRASS || soil == BLOCK_DIRT) || y >= WORLD_MAX_HEIGHT - tree_height - 1) {
    return false;
  }

  chunk_set_block_world(chunk, x, y - 1, z, BLOCK_DIRT);
  build_tree_trunk(chunk, x, y, z, tree_height);
  build_large_oak_leaves(chunk, random, x, y, z, tree_height);
  return true;
}

static TreeType choose_tree_type(BiomeType biome, Random *random) {
  switch (biome) {
  case BIOME_FOREST:
    if (Random_NextIntBounded(random, 5) == 0) {
      return TREE_BIRCH;
    }
    if (Random_NextIntBounded(random, 3) == 0) {
      return TREE_LARGE_OAK;
    }
    return TREE_OAK;
  case BIOME_RAINFOREST:
    return (Random_NextIntBounded(random, 3) == 0) ? TREE_LARGE_OAK : TREE_OAK;
  case BIOME_TAIGA:
    return (Random_NextIntBounded(random, 3) == 0) ? TREE_PINE : TREE_SPRUCE;
  default:
    return (Random_NextIntBounded(random, 10) == 0) ? TREE_LARGE_OAK : TREE_OAK;
  }
}

static bool generate_tree(TreeType type, Chunk *chunk, Random *random, int x, int y, int z) {
  switch (type) {
  case TREE_BIRCH:
    return generate_birch_tree(chunk, random, x, y, z);
  case TREE_SPRUCE:
    return generate_spruce_tree(chunk, random, x, y, z);
  case TREE_PINE:
    return generate_pine_tree(chunk, random, x, y, z);
  case TREE_LARGE_OAK:
    return generate_large_oak_tree(chunk, random, x, y, z);
  case TREE_OAK:
  default:
    return generate_oak_tree(chunk, random, x, y, z);
  }
}

static void generate_tall_grass_patch(Chunk *chunk, Random *random, int x, int y, int z) {
  for (;;) {
    uint8_t block_id = chunk_get_block_world(chunk, x, y, z);
    if ((block_id != BLOCK_AIR && block_id != BLOCK_LEAVES) || y <= 0) {
      break;
    }
    y--;
  }

  for (int i = 0; i < 128; i++) {
    int gx = x + Random_NextIntBounded(random, 8) - Random_NextIntBounded(random, 8);
    int gy = y + Random_NextIntBounded(random, 4) - Random_NextIntBounded(random, 4);
    int gz = z + Random_NextIntBounded(random, 8) - Random_NextIntBounded(random, 8);

    if (can_grow_tall_grass(chunk, gx, gy, gz)) {
      chunk_set_block_world(chunk, gx, gy, gz, BLOCK_TALL_GRASS);
    }
  }
}

static int tree_count_for_biome(BiomeType biome, int tree_density_sample) {
  int count = 0;
  if (biome == BIOME_FOREST || biome == BIOME_RAINFOREST || biome == BIOME_TAIGA) {
    count += tree_density_sample + 5;
  } else if (biome == BIOME_SEASONAL_FOREST) {
    count += tree_density_sample + 2;
  } else if (biome == BIOME_DESERT || biome == BIOME_TUNDRA || biome == BIOME_PLAINS) {
    count -= 20;
  }
  return count;
}

static int tallgrass_count_for_biome(BiomeType biome) {
  if (biome == BIOME_FOREST) {
    return 2;
  }
  if (biome == BIOME_RAINFOREST) {
    return 10;
  }
  if (biome == BIOME_SEASONAL_FOREST) {
    return 2;
  }
  if (biome == BIOME_TAIGA) {
    return 1;
  }
  if (biome == BIOME_PLAINS) {
    return 10;
  }
  return 0;
}

static void decorate_features(WorldGen *gen, Chunk *chunk) {
  int chunk_x = chunk->cx;
  int chunk_z = chunk->cz;
  int block_x = chunk_x * WORLD_CHUNK_SIZE_X;
  int block_z = chunk_z * WORLD_CHUNK_SIZE_Z;
  BiomeType chunk_biome =
      sample_biome_at(gen, block_x + WORLD_CHUNK_SIZE_X / 2, block_z + WORLD_CHUNK_SIZE_Z / 2);

  Random deco_random;
  Random_Init(&deco_random, gen->seed);
  int64_t x_offset = (Random_NextLong(&deco_random) / 2LL) * 2LL + 1LL;
  int64_t z_offset = (Random_NextLong(&deco_random) / 2LL) * 2LL + 1LL;
  Random_SetSeed(&deco_random,
                 ((int64_t)chunk_x * x_offset + (int64_t)chunk_z * z_offset) ^ gen->seed);

  double forest_noise =
      OctavePerlin_Generate2D(&gen->forest_noise, (double)block_x * 0.5, (double)block_z * 0.5);
  int tree_density_sample =
      (int)((forest_noise / 8.0 + Random_NextDouble(&deco_random) * 4.0 + 4.0) / 3.0);
  int number_of_trees = 0;
  if (Random_NextIntBounded(&deco_random, 10) == 0) {
    number_of_trees++;
  }
  number_of_trees += tree_count_for_biome(chunk_biome, tree_density_sample);
  if (number_of_trees < 0) {
    number_of_trees = 0;
  }

  for (int i = 0; i < number_of_trees; i++) {
    int local_x = 3 + Random_NextIntBounded(&deco_random, WORLD_CHUNK_SIZE_X - 6);
    int local_z = 3 + Random_NextIntBounded(&deco_random, WORLD_CHUNK_SIZE_Z - 6);
    int feature_x = block_x + local_x;
    int feature_z = block_z + local_z;
    int feature_y = 0;
    for (int y = WORLD_MAX_HEIGHT - 1; y >= 0; y--) {
      if (chunk_get_block_world(chunk, feature_x, y, feature_z) != BLOCK_AIR &&
          chunk_get_block_world(chunk, feature_x, y, feature_z) != BLOCK_WATER) {
        feature_y = y + 1;
        break;
      }
    }
    TreeType tree_type = choose_tree_type(chunk_biome, &deco_random);
    generate_tree(tree_type, chunk, &deco_random, feature_x, feature_y, feature_z);
  }

  int tallgrass_count = tallgrass_count_for_biome(chunk_biome);
  for (int i = 0; i < tallgrass_count; i++) {
    int feature_x = block_x + Random_NextIntBounded(&deco_random, WORLD_CHUNK_SIZE_X);
    int feature_y = Random_NextIntBounded(&deco_random, WORLD_MAX_HEIGHT);
    int feature_z = block_z + Random_NextIntBounded(&deco_random, WORLD_CHUNK_SIZE_Z);
    generate_tall_grass_patch(chunk, &deco_random, feature_x, feature_y, feature_z);
  }
}

static double *generate_height_map(WorldGen *gen, double *height_map, int x, int y, int z,
                                   int size_x, int size_y, int size_z) {
  size_t total_size = (size_t)size_x * (size_t)size_y * (size_t)size_z;

  if (height_map == NULL) {
    height_map = (double *)malloc(total_size * sizeof(double));
    if (height_map == NULL) {
      return NULL;
    }
  }

  const double horizontal_scale = 684.412;
  const double vertical_scale = 684.412;

  double *temperature_buffer = gen->temperature_map;
  double *downfall_buffer = gen->downfall_map;

  gen->scale_noise_buffer =
      OctavePerlin_Create2D(&gen->floating_island_scale, gen->scale_noise_buffer, x, z, size_x,
                            size_z, 1.121, 1.121, 0.5);

  gen->depth_noise_buffer =
      OctavePerlin_Create2D(&gen->floating_island_noise, gen->depth_noise_buffer, x, z, size_x,
                            size_z, 200.0, 200.0, 0.5);

  gen->selector_noise_buffer = OctavePerlin_Create3D(
      &gen->selector_noise, gen->selector_noise_buffer, (double)x, (double)y, (double)z, size_x,
      size_y, size_z, horizontal_scale / 80.0, vertical_scale / 160.0, horizontal_scale / 80.0);

  gen->min_limit_buffer = OctavePerlin_Create3D(
      &gen->min_limit_perlin, gen->min_limit_buffer, (double)x, (double)y, (double)z, size_x,
      size_y, size_z, horizontal_scale, vertical_scale, horizontal_scale);

  gen->max_limit_buffer = OctavePerlin_Create3D(
      &gen->max_limit_perlin, gen->max_limit_buffer, (double)x, (double)y, (double)z, size_x,
      size_y, size_z, horizontal_scale, vertical_scale, horizontal_scale);

  if (gen->scale_noise_buffer == NULL || gen->depth_noise_buffer == NULL ||
      gen->selector_noise_buffer == NULL || gen->min_limit_buffer == NULL ||
      gen->max_limit_buffer == NULL || temperature_buffer == NULL || downfall_buffer == NULL) {
    return height_map;
  }

  int xyz_index = 0;
  int xz_index = 0;
  int scale_fraction = 16 / size_x;

  for (int ix = 0; ix < size_x; ix++) {
    int sample_x = ix * scale_fraction + scale_fraction / 2;

    for (int iz = 0; iz < size_z; iz++) {
      int sample_z = iz * scale_fraction + scale_fraction / 2;

      double temperature_sample = temperature_buffer[sample_x * 16 + sample_z];
      double downfall_sample = downfall_buffer[sample_x * 16 + sample_z] * temperature_sample;
      downfall_sample = 1.0 - downfall_sample;
      downfall_sample *= downfall_sample;
      downfall_sample *= downfall_sample;
      downfall_sample = 1.0 - downfall_sample;

      double scale_noise_sample = (gen->scale_noise_buffer[xz_index] + 256.0) / 512.0;
      scale_noise_sample *= downfall_sample;
      if (scale_noise_sample > 1.0) {
        scale_noise_sample = 1.0;
      }

      double depth_noise_sample = gen->depth_noise_buffer[xz_index] / 8000.0;
      if (depth_noise_sample < 0.0) {
        depth_noise_sample = -depth_noise_sample * 0.3;
      }

      depth_noise_sample = depth_noise_sample * 3.0 - 2.0;
      if (depth_noise_sample < 0.0) {
        depth_noise_sample /= 2.0;
        if (depth_noise_sample < -1.0) {
          depth_noise_sample = -1.0;
        }
        depth_noise_sample /= 1.4;
        depth_noise_sample /= 2.0;
        scale_noise_sample = 0.0;
      } else {
        if (depth_noise_sample > 1.0) {
          depth_noise_sample = 1.0;
        }
        depth_noise_sample /= 8.0;
      }

      if (scale_noise_sample < 0.0) {
        scale_noise_sample = 0.0;
      }

      scale_noise_sample += 0.5;
      depth_noise_sample = depth_noise_sample * (double)size_y / 16.0;
      double elevation_offset = (double)size_y / 2.0 + depth_noise_sample * 4.0;

      xz_index++;

      for (int iy = 0; iy < size_y; iy++) {
        double density_offset = ((double)iy - elevation_offset) * 12.0 / scale_noise_sample;
        if (density_offset < 0.0) {
          density_offset *= 4.0;
        }

        double low_noise = gen->min_limit_buffer[xyz_index] / 512.0;
        double high_noise = gen->max_limit_buffer[xyz_index] / 512.0;
        double selector_noise_sample = (gen->selector_noise_buffer[xyz_index] / 10.0 + 1.0) / 2.0;

        double terrain_density;
        if (selector_noise_sample < 0.0) {
          terrain_density = low_noise;
        } else if (selector_noise_sample > 1.0) {
          terrain_density = high_noise;
        } else {
          terrain_density = low_noise + (high_noise - low_noise) * selector_noise_sample;
        }

        terrain_density -= density_offset;

        if (iy > size_y - 4) {
          double top_fade = ((double)iy - (double)(size_y - 4)) / 3.0;
          terrain_density = terrain_density * (1.0 - top_fade) + -10.0 * top_fade;
        }

        height_map[xyz_index++] = terrain_density;
      }
    }
  }

  return height_map;
}

static void build_terrain(WorldGen *gen, int chunk_x, int chunk_z, uint8_t *blocks,
                          const BiomeType *biomes, const double *temperatures) {
  (void)biomes;

  const int horiz_scale = 4;
  const int x_max = horiz_scale + 1;
  const int y_max = 17;
  const int z_max = horiz_scale + 1;

  gen->height_map = generate_height_map(gen, gen->height_map, chunk_x * horiz_scale, 0,
                                        chunk_z * horiz_scale, x_max, y_max, z_max);

  if (gen->height_map == NULL) {
    return;
  }

  for (int sample_x = 0; sample_x < horiz_scale; sample_x++) {
    for (int sample_z = 0; sample_z < horiz_scale; sample_z++) {
      for (int sample_y = 0; sample_y < 16; sample_y++) {
        const double vertical_lerp_step = 0.125;

        double corner000 =
            gen->height_map[((sample_x + 0) * z_max + sample_z + 0) * y_max + sample_y + 0];
        double corner010 =
            gen->height_map[((sample_x + 0) * z_max + sample_z + 1) * y_max + sample_y + 0];
        double corner100 =
            gen->height_map[((sample_x + 1) * z_max + sample_z + 0) * y_max + sample_y + 0];
        double corner110 =
            gen->height_map[((sample_x + 1) * z_max + sample_z + 1) * y_max + sample_y + 0];

        double corner001 =
            (gen->height_map[((sample_x + 0) * z_max + sample_z + 0) * y_max + sample_y + 1] -
             corner000) *
            vertical_lerp_step;
        double corner011 =
            (gen->height_map[((sample_x + 0) * z_max + sample_z + 1) * y_max + sample_y + 1] -
             corner010) *
            vertical_lerp_step;
        double corner101 =
            (gen->height_map[((sample_x + 1) * z_max + sample_z + 0) * y_max + sample_y + 1] -
             corner100) *
            vertical_lerp_step;
        double corner111 =
            (gen->height_map[((sample_x + 1) * z_max + sample_z + 1) * y_max + sample_y + 1] -
             corner110) *
            vertical_lerp_step;

        for (int sub_y = 0; sub_y < 8; sub_y++) {
          const double horizontal_lerp_step = 0.25;

          double terrain_x0 = corner000;
          double terrain_x1 = corner010;
          double terrain_step_x0 = (corner100 - corner000) * horizontal_lerp_step;
          double terrain_step_x1 = (corner110 - corner010) * horizontal_lerp_step;

          for (int sub_x = 0; sub_x < 4; sub_x++) {
            int block_index =
                (((sub_x + sample_x * 4) << 11) | ((sample_z * 4) << 7) | ((sample_y * 8) + sub_y));
            double terrain_density = terrain_x0;
            double density_step_z = (terrain_x1 - terrain_x0) * horizontal_lerp_step;

            for (int sub_z = 0; sub_z < 4; sub_z++) {
              int block_type = BLOCK_AIR;

              double temp = temperatures[(sample_x * 4 + sub_x) * 16 + sample_z * 4 + sub_z];
              int y = sample_y * 8 + sub_y;
              if (y < 64) {
                if (temp < 0.5 && y >= 63) {
                  block_type = BLOCK_ICE;
                } else {
                  block_type = BLOCK_WATER;
                }
              }

              if (terrain_density > 0.0) {
                block_type = BLOCK_STONE;
              }

              blocks[block_index] = (uint8_t)block_type;
              block_index += WORLD_MAX_HEIGHT;
              terrain_density += density_step_z;
            }

            terrain_x0 += terrain_step_x0;
            terrain_x1 += terrain_step_x1;
          }

          corner000 += corner001;
          corner010 += corner011;
          corner100 += corner101;
          corner110 += corner111;
        }
      }
    }
  }
}

static void build_surfaces(WorldGen *gen, int chunk_x, int chunk_z, uint8_t *blocks,
                           const BiomeType *biomes) {
  const int sea_level = 64;
  const double chunk_biome = 1.0 / 32.0;

  gen->sand_buffer =
      OctavePerlin_Create3D(&gen->sand_gravel_noise, gen->sand_buffer, (double)(chunk_x * 16),
                            (double)(chunk_z * 16), 0.0, 16, 16, 1, chunk_biome, chunk_biome, 1.0);

  gen->gravel_buffer = OctavePerlin_Create3D(
      &gen->sand_gravel_noise, gen->gravel_buffer, (double)(chunk_x * 16), 109.0134,
      (double)(chunk_z * 16), 16, 1, 16, chunk_biome, 1.0, chunk_biome);

  gen->depth_buffer = OctavePerlin_Create3D(
      &gen->depth_noise, gen->depth_buffer, (double)(chunk_x * 16), (double)(chunk_z * 16), 0.0, 16,
      16, 1, chunk_biome * 2.0, chunk_biome * 2.0, chunk_biome * 2.0);

  if (gen->sand_buffer == NULL || gen->gravel_buffer == NULL || gen->depth_buffer == NULL) {
    return;
  }

  for (int x = 0; x < 16; x++) {
    for (int z = 0; z < 16; z++) {
      int biome_index = x + z * 16;
      BiomeType biome = biomes[biome_index];

      bool use_sand = gen->sand_buffer[biome_index] + Random_NextDouble(&gen->random) * 0.2 > 0.0;
      bool use_gravel =
          gen->gravel_buffer[biome_index] + Random_NextDouble(&gen->random) * 0.2 > 3.0;

      int depth = (int)(gen->depth_buffer[biome_index] / 3.0 + 3.0 +
                        Random_NextDouble(&gen->random) * 0.25);
      int remaining = -1;

      uint8_t biome_top = BIOME_TABLE[biome].top_block;
      uint8_t biome_soil = BIOME_TABLE[biome].soil_block;
      uint8_t column_top = biome_top;
      uint8_t column_soil = biome_soil;

      for (int y = WORLD_MAX_HEIGHT - 1; y >= 0; y--) {
        int idx = (z * 16 + x) * WORLD_MAX_HEIGHT + y;

        if (y <= Random_NextIntBounded(&gen->random, 5)) {
          blocks[idx] = BLOCK_BEDROCK;
          continue;
        }

        uint8_t current = blocks[idx];
        if (current == BLOCK_AIR) {
          remaining = -1;
          continue;
        }

        if (current != BLOCK_STONE) {
          continue;
        }

        if (remaining == -1) {
          if (depth <= 0) {
            column_top = BLOCK_AIR;
            column_soil = BLOCK_STONE;
          } else if (y >= sea_level - 4 && y <= sea_level + 1) {
            column_top = biome_top;
            column_soil = biome_soil;

            if (use_gravel) {
              column_top = BLOCK_AIR;
              column_soil = BLOCK_GRAVEL;
            }

            if (use_sand) {
              column_top = BLOCK_SAND;
              column_soil = BLOCK_SAND;
            }
          }

          if (y < sea_level && column_top == BLOCK_AIR) {
            column_top = BLOCK_WATER;
          }

          remaining = depth;
          if (y >= sea_level - 1) {
            blocks[idx] = column_top;
          } else {
            blocks[idx] = column_soil;
          }
        } else if (remaining > 0) {
          remaining--;
          blocks[idx] = column_soil;

          if (remaining == 0 && column_soil == BLOCK_SAND) {
            remaining = Random_NextIntBounded(&gen->random, 4);
            column_soil = BLOCK_SANDSTONE;
          }
        }
      }
    }
  }
}

void WorldGen_Init(WorldGen *gen, int64_t seed) {
  memset(gen, 0, sizeof(*gen));
  gen->seed = seed;

  Random_Init(&gen->random, seed);

  OctavePerlin_Init(&gen->min_limit_perlin, &gen->random, 16);
  OctavePerlin_Init(&gen->max_limit_perlin, &gen->random, 16);
  OctavePerlin_Init(&gen->selector_noise, &gen->random, 8);
  OctavePerlin_Init(&gen->sand_gravel_noise, &gen->random, 4);
  OctavePerlin_Init(&gen->depth_noise, &gen->random, 4);
  OctavePerlin_Init(&gen->floating_island_scale, &gen->random, 10);
  OctavePerlin_Init(&gen->floating_island_noise, &gen->random, 16);
  OctavePerlin_Init(&gen->forest_noise, &gen->random, 8);

  Random temp_rng;

  Random_Init(&temp_rng, seed * 9871LL);
  OctaveSimplex_Init(&gen->temperature_sampler, &temp_rng, 4);

  Random_Init(&temp_rng, seed * 39811LL);
  OctaveSimplex_Init(&gen->downfall_sampler, &temp_rng, 4);

  Random_Init(&temp_rng, seed * 543321LL);
  OctaveSimplex_Init(&gen->weirdness_sampler, &temp_rng, 2);
}

void WorldGen_Shutdown(WorldGen *gen) {
  free(gen->height_map);
  free(gen->sand_buffer);
  free(gen->gravel_buffer);
  free(gen->depth_buffer);
  free(gen->selector_noise_buffer);
  free(gen->min_limit_buffer);
  free(gen->max_limit_buffer);
  free(gen->scale_noise_buffer);
  free(gen->depth_noise_buffer);
  free(gen->temperature_map);
  free(gen->downfall_map);
  free(gen->weirdness_map);

  memset(gen, 0, sizeof(*gen));
}

void WorldGen_GenerateChunk(WorldGen *gen, Chunk *chunk) {
  BiomeType biomes[16 * 16];
  double temperatures[16 * 16];

  int chunk_x = chunk->cx;
  int chunk_z = chunk->cz;

  int64_t chunk_seed = (int64_t)chunk_x * 341873128712LL + (int64_t)chunk_z * 132897987541LL;
  Random_SetSeed(&gen->random, chunk_seed);

  sample_biomes(gen, chunk_x * 16, chunk_z * 16, 16, 16, biomes, temperatures);

  memset(chunk->blocks, 0, WORLD_CHUNK_VOLUME * sizeof(uint8_t));
  memset(chunk->skylight, 0, WORLD_CHUNK_LIGHT_BYTES * sizeof(uint8_t));

  build_terrain(gen, chunk_x, chunk_z, chunk->blocks, biomes, temperatures);
  build_surfaces(gen, chunk_x, chunk_z, chunk->blocks, biomes);
  decorate_features(gen, chunk);

  Chunk_RecomputeHeightAll(chunk);

  chunk->generated = true;
  chunk->mesh_dirty = true;
  chunk->lighting_dirty = true;
}

static const char *biome_names[] = {"Rainforest", "Swampland", "SeasonalForest", "Forest",
                                    "Savanna",    "Shrubland", "Taiga",          "Desert",
                                    "Plains",     "IceDesert", "Tundra"};

const char *WorldGen_GetBiomeName(const WorldGen *gen, int x, int z) {
  double temperature_sample[1] = {0.0};
  double downfall_sample[1] = {0.0};
  double weirdness_sample[1] = {0.0};

  double *temp_buf = OctaveSimplex_Sample(&gen->temperature_sampler, temperature_sample, (double)x,
                                          (double)z, 1, 1, 0.025, 0.025, 0.25, 0.5);
  double *down_buf = OctaveSimplex_Sample(&gen->downfall_sampler, downfall_sample, (double)x,
                                          (double)z, 1, 1, 0.05, 0.05, 1.0 / 3.0, 0.5);
  double *weird_buf = OctaveSimplex_Sample(&gen->weirdness_sampler, weirdness_sample, (double)x,
                                           (double)z, 1, 1, 0.25, 0.25, 0.5882352941176471, 0.5);

  double raw_temperature = temp_buf ? temp_buf[0] : 0.5;
  double raw_downfall = down_buf ? down_buf[0] : 0.5;
  double raw_weirdness = weird_buf ? weird_buf[0] : 0.0;

  double temperature = 0.5;
  double downfall = 0.5;
  normalize_biome_inputs(raw_temperature, raw_downfall, raw_weirdness, &temperature, &downfall);

  BiomeType biome = locate_biome(temperature, downfall);
  return biome_names[biome];
}
