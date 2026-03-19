#include "math/random.h"

#include <math.h>
#include <stdint.h>

static const int64_t MULTIPLIER = 0x5DEECE66DLL;
static const int64_t ADDEND = 0xBLL;
static const int64_t MASK = (1LL << 48) - 1;

static const float FLOAT_UNIT = 1.0f / (float)(1 << 24);
static const double DOUBLE_UNIT = 1.0 / (double)(1LL << 53);

static int64_t initial_scramble(int64_t seed) { return (seed ^ MULTIPLIER) & MASK; }

static int next_bits(Random *random, int bits) {
  random->seed = (random->seed * MULTIPLIER + ADDEND) & MASK;
  return (int)((uint64_t)random->seed >> (48 - bits));
}

void Random_Init(Random *random, int64_t seed) { Random_SetSeed(random, seed); }

void Random_SetSeed(Random *random, int64_t seed) {
  random->seed = initial_scramble(seed);
  random->have_next_next_gaussian = false;
  random->next_next_gaussian = 0.0;
}

int Random_NextInt(Random *random) { return next_bits(random, 32); }

int Random_NextIntBounded(Random *random, int bound) {
  if (bound <= 0) {
    return 0;
  }

  int r = next_bits(random, 31);
  int m = bound - 1;

  if ((bound & m) == 0) {
    return (int)((bound * (int64_t)r) >> 31);
  }

  for (int u = r; u - (r = u % bound) + m < 0; u = next_bits(random, 31)) {
  }

  return r;
}

int64_t Random_NextLong(Random *random) {
  int64_t hi = (int64_t)next_bits(random, 32);
  int64_t lo = (int64_t)next_bits(random, 32);
  return (hi << 32) + lo;
}

float Random_NextFloat(Random *random) { return (float)next_bits(random, 24) * FLOAT_UNIT; }

double Random_NextDouble(Random *random) {
  int64_t hi = (int64_t)next_bits(random, 26);
  int64_t lo = (int64_t)next_bits(random, 27);
  return ((hi << 27) + lo) * DOUBLE_UNIT;
}

bool Random_NextBoolean(Random *random) { return next_bits(random, 1) != 0; }

double Random_NextGaussian(Random *random) {
  if (random->have_next_next_gaussian) {
    random->have_next_next_gaussian = false;
    return random->next_next_gaussian;
  }

  double v1 = 0.0;
  double v2 = 0.0;
  double s = 0.0;

  do {
    v1 = 2.0 * Random_NextDouble(random) - 1.0;
    v2 = 2.0 * Random_NextDouble(random) - 1.0;
    s = v1 * v1 + v2 * v2;
  } while (s >= 1.0 || s == 0.0);

  double multiplier = sqrt(-2.0 * log(s) / s);
  random->next_next_gaussian = v2 * multiplier;
  random->have_next_next_gaussian = true;

  return v1 * multiplier;
}
