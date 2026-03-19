#ifndef RLVOXEL_CONSTANTS_H
#define RLVOXEL_CONSTANTS_H

/* ============================================================================
 * WORLD CONSTANTS
 * ============================================================================ */

/** Maximum height of the world in blocks */
#define WORLD_MAX_HEIGHT 128

/** Size of each chunk along X axis (in blocks) */
#define WORLD_CHUNK_SIZE_X 16
/** Size of each chunk along Z axis (in blocks) */
#define WORLD_CHUNK_SIZE_Z 16

/* Derived chunk values */
#define WORLD_CHUNK_VOLUME (WORLD_CHUNK_SIZE_X * WORLD_CHUNK_SIZE_Z * WORLD_MAX_HEIGHT)
/** Light data is packed 2 blocks per byte */
#define WORLD_CHUNK_LIGHT_BYTES (WORLD_CHUNK_VOLUME / 2)

/* ============================================================================
 * PLAYER CONSTANTS
 * ============================================================================ */

/** Height of player collision box */
#define PLAYER_HEIGHT 1.80f
/** Height of player's eyes from feet */
#define PLAYER_EYE_HEIGHT 1.62f
/** Half-width of player (radius in X/Z) */
#define PLAYER_HALF_WIDTH 0.30f
/** Maximum distance for block interaction */
#define PLAYER_REACH_DISTANCE 6.0f

/** Sprint acceleration multiplier for tick-based movement */
#define PLAYER_SPRINT_ACCEL_MULTIPLIER 1.5f

/** Tick movement acceleration (blocks/tick) */
#define PLAYER_TICK_MOVE_GROUND 0.10f
#define PLAYER_TICK_MOVE_AIR 0.02f
#define PLAYER_TICK_MOVE_LIQUID 0.02f

/** Tick-based jump/fall model (minecraftc-like) */
#define PLAYER_TICK_JUMP_IMPULSE 0.42f
#define PLAYER_TICK_AIR_GRAVITY 0.08f
#define PLAYER_TICK_AIR_DRAG 0.98f
#define PLAYER_TICK_XZ_DRAG 0.91f
#define PLAYER_TICK_GROUND_DRAG 0.60f

/** Tick-based liquid model (minecraftc-like) */
#define PLAYER_TICK_WATER_DRAG 0.80f
#define PLAYER_TICK_LAVA_DRAG 0.50f
#define PLAYER_TICK_LIQUID_GRAVITY 0.02f
#define PLAYER_TICK_LIQUID_JUMP_BOOST 0.04f
#define PLAYER_TICK_LIQUID_POP_UP 0.30f

/* ============================================================================
 * RENDERING CONSTANTS
 * ============================================================================ */

/** Default number of chunks to render in each direction */
#define DEFAULT_RENDER_DISTANCE 6

/* ============================================================================
 * SIMULATION CONSTANTS
 * ============================================================================ */

/** Fixed simulation rate (ticks/second) */
#define GAME_TICK_RATE 20.0f
/** Fixed simulation delta time (seconds/tick) */
#define GAME_TICK_DT (1.0f / GAME_TICK_RATE)
/** Maximum frame delta fed into accumulator (seconds) */
#define GAME_MAX_FRAME_DELTA 0.25
/** Maximum simulation ticks executed per rendered frame */
#define GAME_MAX_TICKS_PER_FRAME 5

/* ============================================================================
 * PHYSICS CONSTANTS
 * ============================================================================ */

/** Small value to shrink AABB for collision detection */
#define PHYSICS_EPSILON 0.001f
/** Maximum movement distance per physics sub-step */
#define PHYSICS_STEP_SIZE 0.05f

#endif /* RLVOXEL_CONSTANTS_H */
