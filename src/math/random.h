#ifndef RLVOXEL_RANDOM_H
#define RLVOXEL_RANDOM_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Simple random number generator (Java-style).
 * Used for deterministic seeded random generation.
 */
typedef struct Random {
  int64_t seed;
  bool have_next_next_gaussian;
  double next_next_gaussian;
} Random;

/**
 * Initializes a random number generator with a seed.
 */
void Random_Init(Random *random, int64_t seed);

/**
 * Resets the random generator to a new seed.
 */
void Random_SetSeed(Random *random, int64_t seed);

/**
 * Returns the next random integer (full range).
 */
int Random_NextInt(Random *random);

/**
 * Returns a random integer from 0 to bound-1.
 */
int Random_NextIntBounded(Random *random, int bound);

/**
 * Returns the next random long integer.
 */
int64_t Random_NextLong(Random *random);

/**
 * Returns a random float from 0.0 to 1.0.
 */
float Random_NextFloat(Random *random);

/**
 * Returns a random double from 0.0 to 1.0.
 */
double Random_NextDouble(Random *random);

/**
 * Returns a random boolean.
 */
bool Random_NextBoolean(Random *random);

/**
 * Returns a random number with Gaussian (normal) distribution.
 * Mean = 0.0, standard deviation = 1.0.
 */
double Random_NextGaussian(Random *random);

#endif /* RLVOXEL_RANDOM_H */
