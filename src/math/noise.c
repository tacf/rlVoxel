#include "math/random.h"
#define FNL_IMPL
#include "math/noise.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static fnl_state noise_create_state(fnl_noise_type noise_type, int seed) {
  fnl_state state = fnlCreateState();
  state.seed = seed;
  state.noise_type = noise_type;
  state.frequency = 1.0f;
  state.fractal_type = FNL_FRACTAL_NONE;
  state.rotation_type_3d = FNL_ROTATION_NONE;
  return state;
}

static int noise_random_seed(Random *random) {
  if (random == NULL) {
    return 1337;
  }
  return Random_NextInt(random);
}

static double noise_random_coord(Random *random) {
  if (random == NULL) {
    return 0.0;
  }
  return Random_NextDouble(random) * 256.0;
}

void PerlinNoise_Init(PerlinNoiseSampler *sampler, Random *random) {
  if (sampler == NULL) {
    return;
  }

  sampler->x_coord = noise_random_coord(random);
  sampler->y_coord = noise_random_coord(random);
  sampler->z_coord = noise_random_coord(random);
  sampler->state = noise_create_state(FNL_NOISE_PERLIN, noise_random_seed(random));
}

double PerlinNoise_Generate2D(const PerlinNoiseSampler *sampler, double x, double y) {
  if (sampler == NULL) {
    return 0.0;
  }

  float value =
      fnlGetNoise2D(&sampler->state, (float)(x + sampler->x_coord), (float)(y + sampler->y_coord));
  return (double)value;
}

void PerlinNoise_Sample(const PerlinNoiseSampler *sampler, double *buffer, double x_start,
                        double y_start, double z_start, int x_size, int y_size, int z_size,
                        double x_frequency, double y_frequency, double z_frequency,
                        double inverse_amplitude) {
  if (sampler == NULL || buffer == NULL || x_size <= 0 || y_size <= 0 || z_size <= 0) {
    return;
  }

  int counter = 0;
  double amplitude = (inverse_amplitude != 0.0) ? (1.0 / inverse_amplitude) : 1.0;

  for (int x = 0; x < x_size; x++) {
    double nx = (x_start + x) * x_frequency + sampler->x_coord;

    for (int z = 0; z < z_size; z++) {
      double nz = (z_start + z) * z_frequency + sampler->z_coord;

      for (int y = 0; y < y_size; y++) {
        double ny = (y_start + y) * y_frequency + sampler->y_coord;

        float sample = fnlGetNoise3D(&sampler->state, (float)nx, (float)ny, (float)nz);
        buffer[counter++] += (double)sample * amplitude;
      }
    }
  }
}

void OctavePerlin_Init(OctavePerlinNoiseSampler *sampler, Random *random, int octave_count) {
  if (sampler == NULL) {
    return;
  }

  if (octave_count < 1) {
    octave_count = 1;
  }
  if (octave_count > NOISE_MAX_OCTAVES) {
    octave_count = NOISE_MAX_OCTAVES;
  }

  sampler->octave_count = octave_count;

  for (int i = 0; i < sampler->octave_count; i++) {
    PerlinNoise_Init(&sampler->octaves[i], random);
  }
}

double OctavePerlin_Generate2D(const OctavePerlinNoiseSampler *sampler, double x, double y) {
  if (sampler == NULL) {
    return 0.0;
  }

  double value = 0.0;
  double amplitude = 1.0;

  for (int i = 0; i < sampler->octave_count; i++) {
    value += PerlinNoise_Generate2D(&sampler->octaves[i], x * amplitude, y * amplitude) / amplitude;
    amplitude /= 2.0;
  }

  return value;
}

double *OctavePerlin_Create3D(const OctavePerlinNoiseSampler *sampler, double *buffer,
                              double x_start, double y_start, double z_start, int x_size,
                              int y_size, int z_size, double x_frequency, double y_frequency,
                              double z_frequency) {
  if (sampler == NULL || x_size <= 0 || y_size <= 0 || z_size <= 0) {
    return buffer;
  }

  size_t count = (size_t)x_size * (size_t)y_size * (size_t)z_size;
  if (buffer == NULL) {
    buffer = (double *)malloc(count * sizeof(double));
  }
  if (buffer == NULL) {
    return NULL;
  }

  memset(buffer, 0, count * sizeof(double));

  double octave_multiplier = 1.0;
  for (int i = 0; i < sampler->octave_count; i++) {
    PerlinNoise_Sample(&sampler->octaves[i], buffer, x_start, y_start, z_start, x_size, y_size,
                       z_size, x_frequency * octave_multiplier, y_frequency * octave_multiplier,
                       z_frequency * octave_multiplier, octave_multiplier);
    octave_multiplier /= 2.0;
  }

  return buffer;
}

double *OctavePerlin_Create2D(const OctavePerlinNoiseSampler *sampler, double *buffer, int x_start,
                              int z_start, int x_size, int z_size, double x_frequency,
                              double z_frequency, double inverse_amplitude) {
  (void)inverse_amplitude;
  return OctavePerlin_Create3D(sampler, buffer, (double)x_start, 10.0, (double)z_start, x_size, 1,
                               z_size, x_frequency, 1.0, z_frequency);
}

void SimplexNoise_Init(SimplexNoiseSampler *sampler, Random *random) {
  if (sampler == NULL) {
    return;
  }

  sampler->x_coord = noise_random_coord(random);
  sampler->y_coord = noise_random_coord(random);
  sampler->z_coord = noise_random_coord(random);
  sampler->state = noise_create_state(FNL_NOISE_OPENSIMPLEX2, noise_random_seed(random));
}

void SimplexNoise_Sample(const SimplexNoiseSampler *sampler, double *buffer, double x, double z,
                         int width, int depth, double x_frequency, double z_frequency,
                         double amplitude) {
  if (sampler == NULL || buffer == NULL || width <= 0 || depth <= 0) {
    return;
  }

  int counter = 0;

  for (int x1 = 0; x1 < width; x1++) {
    double nx = (x + x1) * x_frequency + sampler->x_coord;

    for (int z1 = 0; z1 < depth; z1++) {
      double nz = (z + z1) * z_frequency + sampler->y_coord;
      float sample = fnlGetNoise2D(&sampler->state, (float)nx, (float)nz);
      buffer[counter++] += (double)sample * amplitude;
    }
  }
}

void OctaveSimplex_Init(OctaveSimplexNoiseSampler *sampler, Random *random, int octave_count) {
  if (sampler == NULL) {
    return;
  }

  if (octave_count < 1) {
    octave_count = 1;
  }
  if (octave_count > NOISE_MAX_OCTAVES) {
    octave_count = NOISE_MAX_OCTAVES;
  }

  sampler->octave_count = octave_count;

  for (int i = 0; i < sampler->octave_count; i++) {
    SimplexNoise_Init(&sampler->octaves[i], random);
  }
}

double *OctaveSimplex_Sample(const OctaveSimplexNoiseSampler *sampler, double *buffer, double x,
                             double z, int width, int depth, double x_frequency, double z_frequency,
                             double frequency_scaler, double amplitude_scaler) {
  if (sampler == NULL || width <= 0 || depth <= 0) {
    return buffer;
  }

  x_frequency /= 1.5;
  z_frequency /= 1.5;

  size_t size = (size_t)width * (size_t)depth;
  if (buffer == NULL) {
    buffer = (double *)malloc(size * sizeof(double));
  }
  if (buffer == NULL) {
    return NULL;
  }

  memset(buffer, 0, size * sizeof(double));

  double amplitude_divisor = 1.0;
  double frequency_multiplier = 1.0;

  for (int i = 0; i < sampler->octave_count; i++) {
    SimplexNoise_Sample(&sampler->octaves[i], buffer, x, z, width, depth,
                        x_frequency * frequency_multiplier, z_frequency * frequency_multiplier,
                        0.55 / amplitude_divisor);

    frequency_multiplier *= frequency_scaler;
    amplitude_divisor *= amplitude_scaler;
  }

  return buffer;
}
