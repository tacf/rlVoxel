#ifndef RLVOXEL_NET_PROTOCOL_H
#define RLVOXEL_NET_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "constants.h"

/**
 * rlVoxel wire protocol v1.
 *
 * Every packet begins with NetMessageHeader, followed by a message-specific
 * payload. Encode/decode APIs return false on malformed or undersized data.
 */

/** Wire magic ('RVXL') present in every packet header. */
#define RVNET_MAGIC 0x5256584Cu /* 'RVXL' */
/** Protocol version for v1 compatibility checks. */
#define RVNET_VERSION 1u
/** Max UTF-8-safe disconnect reason bytes (excluding trailing NUL). */
#define RVNET_MAX_DISCONNECT_REASON 128u

/** v1 message ids shared by client and server. */
typedef enum NetMessageType {
  NET_MSG_C2S_HELLO = 1,
  NET_MSG_S2C_WELCOME = 2,
  NET_MSG_C2S_INPUT_CMD = 3,
  NET_MSG_S2C_PLAYER_STATE = 4,
  NET_MSG_S2C_CHUNK_DATA = 5,
  NET_MSG_S2C_BLOCK_DELTA = 6,
  NET_MSG_S2C_CHUNK_UNLOAD = 7,
  NET_MSG_S2C_DISCONNECT = 8,
  NET_MSG_C2S_PLAYER_MOVE = 9,
} NetMessageType;

/** Bitfield flags for GameplayInputCmd.buttons. */
enum {
  GAMEPLAY_INPUT_MOVE_FORWARD = 1u << 0,
  GAMEPLAY_INPUT_MOVE_BACKWARD = 1u << 1,
  GAMEPLAY_INPUT_MOVE_LEFT = 1u << 2,
  GAMEPLAY_INPUT_MOVE_RIGHT = 1u << 3,
  GAMEPLAY_INPUT_SPRINT = 1u << 4,
  GAMEPLAY_INPUT_JUMP_HELD = 1u << 5,
  GAMEPLAY_INPUT_LEFT_CLICK = 1u << 6,
  GAMEPLAY_INPUT_RIGHT_CLICK = 1u << 7,
};

/** Common packet header prepended to every encoded message. */
typedef struct NetMessageHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t type;
  uint32_t sequence;
  uint32_t tick;
} NetMessageHeader;

/** Client hello payload (render distance request). */
typedef struct NetHello {
  uint32_t requested_render_distance;
} NetHello;

/** Server welcome payload sent after hello acceptance. */
typedef struct NetWelcome {
  int64_t seed;
  int32_t render_distance;
  int32_t tick_rate;
} NetWelcome;

/**
 * Client gameplay action command.
 *
 * Used for edge-triggered actions (clicks) and selected block authority.
 * Movement authority is carried by NetPlayerMove.
 */
typedef struct GameplayInputCmd {
  uint32_t tick_id;
  uint8_t buttons;
  uint8_t selected_block;
  float look_delta_x;
  float look_delta_y;
} GameplayInputCmd;

/**
 * Client-reported movement snapshot.
 *
 * - Client sends movement pose updates each tick (or at unchanged reminder cadence).
 * - Server validates and either accepts pose or emits correction state.
 */
typedef struct NetPlayerMove {
  uint32_t tick_id;
  float position_x;
  float position_y;
  float position_z;
  float velocity_x;
  float velocity_y;
  float velocity_z;
  float yaw;
  float pitch;
  uint8_t on_ground;
} NetPlayerMove;

/**
 * Authoritative player snapshot streamed by server.
 *
 * Sent for correction and periodic sync.
 */
typedef struct AuthoritativePlayerState {
  /* Server simulation tick for this snapshot. */
  uint32_t tick_id;
  /*
   * Most recent client NetPlayerMove.tick_id accepted by server when
   * producing this snapshot. Used for client reconciliation.
   */
  uint32_t input_tick_id;
  float position_x;
  float position_y;
  float position_z;
  float velocity_x;
  float velocity_y;
  float velocity_z;
  float yaw;
  float pitch;
  uint8_t on_ground;
  float world_time;
} AuthoritativePlayerState;

/** Borrowed chunk payload view for encode path. */
typedef struct NetChunkData {
  int32_t cx;
  int32_t cz;
  const uint8_t *blocks;
  const uint8_t *skylight;
  const uint8_t *heightmap;
} NetChunkData;

/** Owned chunk payload for decode path. */
typedef struct NetChunkDataOwned {
  int32_t cx;
  int32_t cz;
  uint8_t blocks[WORLD_CHUNK_VOLUME];
  uint8_t skylight[WORLD_CHUNK_LIGHT_BYTES];
  uint8_t heightmap[WORLD_CHUNK_SIZE_X * WORLD_CHUNK_SIZE_Z];
} NetChunkDataOwned;

/** Single-block authoritative delta. */
typedef struct NetBlockDelta {
  int32_t x;
  int32_t y;
  int32_t z;
  uint8_t block_id;
  uint8_t skylight;
} NetBlockDelta;

/** Chunk unload notification. */
typedef struct NetChunkUnload {
  int32_t cx;
  int32_t cz;
} NetChunkUnload;

/** Disconnect reason payload. */
typedef struct NetDisconnect {
  char reason[RVNET_MAX_DISCONNECT_REASON];
} NetDisconnect;

/** Returns fixed encoded header size in bytes. */
size_t Protocol_HeaderSize(void);
/** Returns stable display name for a NetMessageType (never NULL). */
const char *Protocol_MessageTypeName(NetMessageType type);
/**
 * Returns fixed payload size for a message type, or 0 when payload is variable-length.
 * Current variable-length type: NET_MSG_S2C_DISCONNECT.
 */
size_t Protocol_FixedPayloadSize(NetMessageType type);
/**
 * Returns fixed packet size (header + payload) for message type, or 0 for variable-length types.
 */
size_t Protocol_FixedPacketSize(NetMessageType type);
/**
 * Validates and parses header fields from a packet buffer.
 * Checks magic/version/type and minimum packet size.
 */
bool Protocol_ParseHeader(const uint8_t *data, size_t size, NetMessageHeader *out_header);

/**
 * Encode/decode helpers for each v1 message type.
 * All decode helpers parse and return NetMessageHeader in out_header.
 * Encode helpers write encoded packet bytes into caller-provided `out`.
 * Decode helpers validate header + payload size before writing outputs.
 */
bool Protocol_EncodeHello(uint32_t sequence, uint32_t tick, const NetHello *msg, uint8_t *out,
                          size_t out_cap, size_t *out_size);
bool Protocol_DecodeHello(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                          NetHello *out_msg);

bool Protocol_EncodeWelcome(uint32_t sequence, uint32_t tick, const NetWelcome *msg, uint8_t *out,
                            size_t out_cap, size_t *out_size);
bool Protocol_DecodeWelcome(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                            NetWelcome *out_msg);

bool Protocol_EncodeInputCmd(uint32_t sequence, uint32_t tick, const GameplayInputCmd *msg,
                             uint8_t *out, size_t out_cap, size_t *out_size);
bool Protocol_DecodeInputCmd(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                             GameplayInputCmd *out_msg);

bool Protocol_EncodePlayerMove(uint32_t sequence, uint32_t tick, const NetPlayerMove *msg,
                               uint8_t *out, size_t out_cap, size_t *out_size);
bool Protocol_DecodePlayerMove(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                               NetPlayerMove *out_msg);

bool Protocol_EncodePlayerState(uint32_t sequence, uint32_t tick,
                                const AuthoritativePlayerState *msg, uint8_t *out, size_t out_cap,
                                size_t *out_size);
bool Protocol_DecodePlayerState(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                                AuthoritativePlayerState *out_msg);

bool Protocol_EncodeChunkData(uint32_t sequence, uint32_t tick, const NetChunkData *msg,
                              uint8_t *out, size_t out_cap, size_t *out_size);
bool Protocol_DecodeChunkData(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                              NetChunkDataOwned *out_msg);

bool Protocol_EncodeBlockDelta(uint32_t sequence, uint32_t tick, const NetBlockDelta *msg,
                               uint8_t *out, size_t out_cap, size_t *out_size);
bool Protocol_DecodeBlockDelta(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                               NetBlockDelta *out_msg);

bool Protocol_EncodeChunkUnload(uint32_t sequence, uint32_t tick, const NetChunkUnload *msg,
                                uint8_t *out, size_t out_cap, size_t *out_size);
bool Protocol_DecodeChunkUnload(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                                NetChunkUnload *out_msg);

bool Protocol_EncodeDisconnect(uint32_t sequence, uint32_t tick, const NetDisconnect *msg,
                               uint8_t *out, size_t out_cap, size_t *out_size);
bool Protocol_DecodeDisconnect(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                               NetDisconnect *out_msg);

#endif
