#ifndef RLVOXEL_NOISE_H
#define RLVOXEL_NOISE_H

#include <stdint.h>

#include "math/random.h"
#include "FastNoiseLite.h"

/** Maximum number of octaves for fractal noise */
#define NOISE_MAX_OCTAVES 16

/**
 * Perlin noise sampler for smooth gradient noise.
 */
typedef struct PerlinNoiseSampler {
  fnl_state state;
  double x_coord;
  double y_coord;
  double z_coord;
} PerlinNoiseSampler;

/**
 * Fractal Perlin noise using multiple octaves.
 */
typedef struct OctavePerlinNoiseSampler {
  PerlinNoiseSampler octaves[NOISE_MAX_OCTAVES];
  int octave_count;
} OctavePerlinNoiseSampler;

/**
 * Simplex-like sampler backed by FastNoiseLite OpenSimplex2.
 */
typedef struct SimplexNoiseSampler {
  fnl_state state;
  double x_coord;
  double y_coord;
  double z_coord;
} SimplexNoiseSampler;

/**
 * Fractal Simplex noise using multiple octaves.
 */
typedef struct OctaveSimplexNoiseSampler {
  SimplexNoiseSampler octaves[NOISE_MAX_OCTAVES];
  int octave_count;
} OctaveSimplexNoiseSampler;

/* ---------------------------------------------------------------------------
 * Perlin Noise Functions
 * --------------------------------------------------------------------------- */

/**
 * Initializes a Perlin noise sampler with a random seed.
 */
void PerlinNoise_Init(PerlinNoiseSampler *sampler, Random *random);

/**
 * Generates a 2D Perlin noise value at x, y.
 */
double PerlinNoise_Generate2D(const PerlinNoiseSampler *sampler, double x, double y);

/**
 * Samples Perlin noise into a 3D buffer.
 */
void PerlinNoise_Sample(const PerlinNoiseSampler *sampler, double *buffer, double x_start,
                        double y_start, double z_start, int x_size, int y_size, int z_size,
                        double x_frequency, double y_frequency, double z_frequency,
                        double inverse_amplitude);

/* ---------------------------------------------------------------------------
 * Octave Perlin Noise Functions
 * --------------------------------------------------------------------------- */

/**
 * Initializes a multi-octave Perlin noise sampler.
 * @param octave_count Number of octaves (layers) to stack
 */
void OctavePerlin_Init(OctavePerlinNoiseSampler *sampler, Random *random, int octave_count);

/**
 * Generates a 2D fractal Perlin noise value.
 */
double OctavePerlin_Generate2D(const OctavePerlinNoiseSampler *sampler, double x, double y);

/**
 * Creates a 3D buffer filled with fractal Perlin noise.
 * @param buffer Pre-allocated buffer (or NULL to allocate)
 * @return Pointer to buffer (allocated if buffer was NULL)
 */
double *OctavePerlin_Create3D(const OctavePerlinNoiseSampler *sampler, double *buffer,
                              double x_start, double y_start, double z_start, int x_size,
                              int y_size, int z_size, double x_frequency, double y_frequency,
                              double z_frequency);

/**
 * Creates a 2D buffer filled with fractal Perlin noise.
 */
double *OctavePerlin_Create2D(const OctavePerlinNoiseSampler *sampler, double *buffer, int x_start,
                              int z_start, int x_size, int z_size, double x_frequency,
                              double z_frequency, double inverse_amplitude);

/* ---------------------------------------------------------------------------
 * Simplex Noise Functions
 * --------------------------------------------------------------------------- */

/**
 * Initializes a Simplex noise sampler with a random seed.
 */
void SimplexNoise_Init(SimplexNoiseSampler *sampler, Random *random);

/**
 * Samples Simplex noise into a 2D buffer.
 */
void SimplexNoise_Sample(const SimplexNoiseSampler *sampler, double *buffer, double x, double z,
                         int width, int depth, double x_frequency, double z_frequency,
                         double amplitude);

/* ---------------------------------------------------------------------------
 * Octave Simplex Noise Functions
 * --------------------------------------------------------------------------- */

/**
 * Initializes a multi-octave Simplex noise sampler.
 */
void OctaveSimplex_Init(OctaveSimplexNoiseSampler *sampler, Random *random, int octave_count);

/**
 * Samples fractal Simplex noise into a 2D buffer.
 */
double *OctaveSimplex_Sample(const OctaveSimplexNoiseSampler *sampler, double *buffer, double x,
                             double z, int width, int depth, double x_frequency, double z_frequency,
                             double frequency_scaler, double amplitude_scaler);

#endif /* RLVOXEL_NOISE_H */
