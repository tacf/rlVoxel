#include "net/protocol.h"

#include <string.h>

#define NET_HEADER_BYTES 16u

static bool ensure_space(size_t offset, size_t need, size_t cap) { return (offset + need) <= cap; }

static void write_u16_le(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void write_u32_le(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
  dst[2] = (uint8_t)((value >> 16) & 0xFFu);
  dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void write_u64_le(uint8_t *dst, uint64_t value) {
  dst[0] = (uint8_t)(value & 0xFFu);
  dst[1] = (uint8_t)((value >> 8) & 0xFFu);
  dst[2] = (uint8_t)((value >> 16) & 0xFFu);
  dst[3] = (uint8_t)((value >> 24) & 0xFFu);
  dst[4] = (uint8_t)((value >> 32) & 0xFFu);
  dst[5] = (uint8_t)((value >> 40) & 0xFFu);
  dst[6] = (uint8_t)((value >> 48) & 0xFFu);
  dst[7] = (uint8_t)((value >> 56) & 0xFFu);
}

static uint16_t read_u16_le(const uint8_t *src) {
  return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *src) {
  return (uint32_t)((uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
                    ((uint32_t)src[3] << 24));
}

static uint64_t read_u64_le(const uint8_t *src) {
  return (uint64_t)src[0] | ((uint64_t)src[1] << 8) | ((uint64_t)src[2] << 16) |
         ((uint64_t)src[3] << 24) | ((uint64_t)src[4] << 32) | ((uint64_t)src[5] << 40) |
         ((uint64_t)src[6] << 48) | ((uint64_t)src[7] << 56);
}

static size_t encode_header(uint8_t *out, size_t out_cap, NetMessageType type, uint32_t sequence,
                            uint32_t tick) {
  if (!ensure_space(0, NET_HEADER_BYTES, out_cap)) {
    return 0;
  }

  write_u32_le(out + 0, RVNET_MAGIC);
  write_u16_le(out + 4, (uint16_t)RVNET_VERSION);
  write_u16_le(out + 6, (uint16_t)type);
  write_u32_le(out + 8, sequence);
  write_u32_le(out + 12, tick);
  return NET_HEADER_BYTES;
}

size_t Protocol_HeaderSize(void) { return NET_HEADER_BYTES; }

bool Protocol_ParseHeader(const uint8_t *data, size_t size, NetMessageHeader *out_header) {
  if (data == NULL || out_header == NULL || size < NET_HEADER_BYTES) {
    return false;
  }

  out_header->magic = read_u32_le(data + 0);
  out_header->version = read_u16_le(data + 4);
  out_header->type = read_u16_le(data + 6);
  out_header->sequence = read_u32_le(data + 8);
  out_header->tick = read_u32_le(data + 12);

  if (out_header->magic != RVNET_MAGIC || out_header->version != RVNET_VERSION) {
    return false;
  }

  return true;
}

bool Protocol_EncodeHello(uint32_t sequence, uint32_t tick, const NetHello *msg, uint8_t *out,
                          size_t out_cap, size_t *out_size) {
  if (msg == NULL || out == NULL || out_size == NULL) {
    return false;
  }

  size_t offset = encode_header(out, out_cap, NET_MSG_C2S_HELLO, sequence, tick);
  if (offset == 0 || !ensure_space(offset, 4, out_cap)) {
    return false;
  }

  write_u32_le(out + offset, msg->requested_render_distance);
  offset += 4;
  *out_size = offset;
  return true;
}

bool Protocol_DecodeHello(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                          NetHello *out_msg) {
  if (out_msg == NULL || !Protocol_ParseHeader(data, size, out_header) ||
      out_header->type != NET_MSG_C2S_HELLO || size < NET_HEADER_BYTES + 4) {
    return false;
  }

  out_msg->requested_render_distance = read_u32_le(data + NET_HEADER_BYTES);
  return true;
}

bool Protocol_EncodeWelcome(uint32_t sequence, uint32_t tick, const NetWelcome *msg, uint8_t *out,
                            size_t out_cap, size_t *out_size) {
  if (msg == NULL || out == NULL || out_size == NULL) {
    return false;
  }

  size_t offset = encode_header(out, out_cap, NET_MSG_S2C_WELCOME, sequence, tick);
  if (offset == 0 || !ensure_space(offset, 16, out_cap)) {
    return false;
  }

  write_u64_le(out + offset, (uint64_t)msg->seed);
  offset += 8;
  write_u32_le(out + offset, (uint32_t)msg->render_distance);
  offset += 4;
  write_u32_le(out + offset, (uint32_t)msg->tick_rate);
  offset += 4;

  *out_size = offset;
  return true;
}

bool Protocol_DecodeWelcome(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                            NetWelcome *out_msg) {
  if (out_msg == NULL || !Protocol_ParseHeader(data, size, out_header) ||
      out_header->type != NET_MSG_S2C_WELCOME || size < NET_HEADER_BYTES + 16) {
    return false;
  }

  out_msg->seed = (int64_t)read_u64_le(data + NET_HEADER_BYTES);
  out_msg->render_distance = (int32_t)read_u32_le(data + NET_HEADER_BYTES + 8);
  out_msg->tick_rate = (int32_t)read_u32_le(data + NET_HEADER_BYTES + 12);
  return true;
}

bool Protocol_EncodeInputCmd(uint32_t sequence, uint32_t tick, const GameplayInputCmd *msg,
                             uint8_t *out, size_t out_cap, size_t *out_size) {
  if (msg == NULL || out == NULL || out_size == NULL) {
    return false;
  }

  size_t offset = encode_header(out, out_cap, NET_MSG_C2S_INPUT_CMD, sequence, tick);
  if (offset == 0 || !ensure_space(offset, 14, out_cap)) {
    return false;
  }

  write_u32_le(out + offset, msg->tick_id);
  offset += 4;
  out[offset++] = msg->buttons;
  out[offset++] = msg->selected_block;
  memcpy(out + offset, &msg->look_delta_x, sizeof(float));
  offset += sizeof(float);
  memcpy(out + offset, &msg->look_delta_y, sizeof(float));
  offset += sizeof(float);

  *out_size = offset;
  return true;
}

bool Protocol_DecodeInputCmd(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                             GameplayInputCmd *out_msg) {
  if (out_msg == NULL || !Protocol_ParseHeader(data, size, out_header) ||
      out_header->type != NET_MSG_C2S_INPUT_CMD || size < NET_HEADER_BYTES + 14) {
    return false;
  }

  size_t offset = NET_HEADER_BYTES;
  out_msg->tick_id = read_u32_le(data + offset);
  offset += 4;
  out_msg->buttons = data[offset++];
  out_msg->selected_block = data[offset++];
  memcpy(&out_msg->look_delta_x, data + offset, sizeof(float));
  offset += sizeof(float);
  memcpy(&out_msg->look_delta_y, data + offset, sizeof(float));
  return true;
}

bool Protocol_EncodePlayerState(uint32_t sequence, uint32_t tick,
                                const AuthoritativePlayerState *msg, uint8_t *out, size_t out_cap,
                                size_t *out_size) {
  if (msg == NULL || out == NULL || out_size == NULL) {
    return false;
  }

  size_t offset = encode_header(out, out_cap, NET_MSG_S2C_PLAYER_STATE, sequence, tick);
  if (offset == 0 || !ensure_space(offset, 41, out_cap)) {
    return false;
  }

  write_u32_le(out + offset, msg->tick_id);
  offset += 4;

  memcpy(out + offset, &msg->position_x, sizeof(float));
  offset += sizeof(float);
  memcpy(out + offset, &msg->position_y, sizeof(float));
  offset += sizeof(float);
  memcpy(out + offset, &msg->position_z, sizeof(float));
  offset += sizeof(float);

  memcpy(out + offset, &msg->velocity_x, sizeof(float));
  offset += sizeof(float);
  memcpy(out + offset, &msg->velocity_y, sizeof(float));
  offset += sizeof(float);
  memcpy(out + offset, &msg->velocity_z, sizeof(float));
  offset += sizeof(float);

  memcpy(out + offset, &msg->yaw, sizeof(float));
  offset += sizeof(float);
  memcpy(out + offset, &msg->pitch, sizeof(float));
  offset += sizeof(float);

  out[offset++] = msg->on_ground;

  memcpy(out + offset, &msg->world_time, sizeof(float));
  offset += sizeof(float);

  *out_size = offset;
  return true;
}

bool Protocol_DecodePlayerState(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                                AuthoritativePlayerState *out_msg) {
  if (out_msg == NULL || !Protocol_ParseHeader(data, size, out_header) ||
      out_header->type != NET_MSG_S2C_PLAYER_STATE || size < NET_HEADER_BYTES + 41) {
    return false;
  }

  size_t offset = NET_HEADER_BYTES;
  out_msg->tick_id = read_u32_le(data + offset);
  offset += 4;

  memcpy(&out_msg->position_x, data + offset, sizeof(float));
  offset += sizeof(float);
  memcpy(&out_msg->position_y, data + offset, sizeof(float));
  offset += sizeof(float);
  memcpy(&out_msg->position_z, data + offset, sizeof(float));
  offset += sizeof(float);

  memcpy(&out_msg->velocity_x, data + offset, sizeof(float));
  offset += sizeof(float);
  memcpy(&out_msg->velocity_y, data + offset, sizeof(float));
  offset += sizeof(float);
  memcpy(&out_msg->velocity_z, data + offset, sizeof(float));
  offset += sizeof(float);

  memcpy(&out_msg->yaw, data + offset, sizeof(float));
  offset += sizeof(float);
  memcpy(&out_msg->pitch, data + offset, sizeof(float));
  offset += sizeof(float);

  out_msg->on_ground = data[offset++];

  memcpy(&out_msg->world_time, data + offset, sizeof(float));
  return true;
}

bool Protocol_EncodeChunkData(uint32_t sequence, uint32_t tick, const NetChunkData *msg,
                              uint8_t *out, size_t out_cap, size_t *out_size) {
  size_t payload = 8 + WORLD_CHUNK_VOLUME + WORLD_CHUNK_LIGHT_BYTES +
                   (WORLD_CHUNK_SIZE_X * WORLD_CHUNK_SIZE_Z);

  if (msg == NULL || out == NULL || out_size == NULL || msg->blocks == NULL ||
      msg->skylight == NULL || msg->heightmap == NULL) {
    return false;
  }

  size_t offset = encode_header(out, out_cap, NET_MSG_S2C_CHUNK_DATA, sequence, tick);
  if (offset == 0 || !ensure_space(offset, payload, out_cap)) {
    return false;
  }

  write_u32_le(out + offset, (uint32_t)msg->cx);
  offset += 4;
  write_u32_le(out + offset, (uint32_t)msg->cz);
  offset += 4;

  memcpy(out + offset, msg->blocks, WORLD_CHUNK_VOLUME);
  offset += WORLD_CHUNK_VOLUME;

  memcpy(out + offset, msg->skylight, WORLD_CHUNK_LIGHT_BYTES);
  offset += WORLD_CHUNK_LIGHT_BYTES;

  memcpy(out + offset, msg->heightmap, WORLD_CHUNK_SIZE_X * WORLD_CHUNK_SIZE_Z);
  offset += WORLD_CHUNK_SIZE_X * WORLD_CHUNK_SIZE_Z;

  *out_size = offset;
  return true;
}

bool Protocol_DecodeChunkData(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                              NetChunkDataOwned *out_msg) {
  size_t payload = 8 + WORLD_CHUNK_VOLUME + WORLD_CHUNK_LIGHT_BYTES +
                   (WORLD_CHUNK_SIZE_X * WORLD_CHUNK_SIZE_Z);

  if (out_msg == NULL || !Protocol_ParseHeader(data, size, out_header) ||
      out_header->type != NET_MSG_S2C_CHUNK_DATA || size < NET_HEADER_BYTES + payload) {
    return false;
  }

  size_t offset = NET_HEADER_BYTES;
  out_msg->cx = (int32_t)read_u32_le(data + offset);
  offset += 4;
  out_msg->cz = (int32_t)read_u32_le(data + offset);
  offset += 4;

  memcpy(out_msg->blocks, data + offset, WORLD_CHUNK_VOLUME);
  offset += WORLD_CHUNK_VOLUME;

  memcpy(out_msg->skylight, data + offset, WORLD_CHUNK_LIGHT_BYTES);
  offset += WORLD_CHUNK_LIGHT_BYTES;

  memcpy(out_msg->heightmap, data + offset, WORLD_CHUNK_SIZE_X * WORLD_CHUNK_SIZE_Z);
  return true;
}

bool Protocol_EncodeBlockDelta(uint32_t sequence, uint32_t tick, const NetBlockDelta *msg,
                               uint8_t *out, size_t out_cap, size_t *out_size) {
  if (msg == NULL || out == NULL || out_size == NULL) {
    return false;
  }

  size_t offset = encode_header(out, out_cap, NET_MSG_S2C_BLOCK_DELTA, sequence, tick);
  if (offset == 0 || !ensure_space(offset, 14, out_cap)) {
    return false;
  }

  write_u32_le(out + offset, (uint32_t)msg->x);
  offset += 4;
  write_u32_le(out + offset, (uint32_t)msg->y);
  offset += 4;
  write_u32_le(out + offset, (uint32_t)msg->z);
  offset += 4;
  out[offset++] = msg->block_id;
  out[offset++] = msg->skylight;

  *out_size = offset;
  return true;
}

bool Protocol_DecodeBlockDelta(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                               NetBlockDelta *out_msg) {
  if (out_msg == NULL || !Protocol_ParseHeader(data, size, out_header) ||
      out_header->type != NET_MSG_S2C_BLOCK_DELTA || size < NET_HEADER_BYTES + 14) {
    return false;
  }

  size_t offset = NET_HEADER_BYTES;
  out_msg->x = (int32_t)read_u32_le(data + offset);
  offset += 4;
  out_msg->y = (int32_t)read_u32_le(data + offset);
  offset += 4;
  out_msg->z = (int32_t)read_u32_le(data + offset);
  offset += 4;
  out_msg->block_id = data[offset++];
  out_msg->skylight = data[offset++];
  return true;
}

bool Protocol_EncodeChunkUnload(uint32_t sequence, uint32_t tick, const NetChunkUnload *msg,
                                uint8_t *out, size_t out_cap, size_t *out_size) {
  if (msg == NULL || out == NULL || out_size == NULL) {
    return false;
  }

  size_t offset = encode_header(out, out_cap, NET_MSG_S2C_CHUNK_UNLOAD, sequence, tick);
  if (offset == 0 || !ensure_space(offset, 8, out_cap)) {
    return false;
  }

  write_u32_le(out + offset, (uint32_t)msg->cx);
  offset += 4;
  write_u32_le(out + offset, (uint32_t)msg->cz);
  offset += 4;

  *out_size = offset;
  return true;
}

bool Protocol_DecodeChunkUnload(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                                NetChunkUnload *out_msg) {
  if (out_msg == NULL || !Protocol_ParseHeader(data, size, out_header) ||
      out_header->type != NET_MSG_S2C_CHUNK_UNLOAD || size < NET_HEADER_BYTES + 8) {
    return false;
  }

  out_msg->cx = (int32_t)read_u32_le(data + NET_HEADER_BYTES + 0);
  out_msg->cz = (int32_t)read_u32_le(data + NET_HEADER_BYTES + 4);
  return true;
}

bool Protocol_EncodeDisconnect(uint32_t sequence, uint32_t tick, const NetDisconnect *msg,
                               uint8_t *out, size_t out_cap, size_t *out_size) {
  uint16_t reason_len;
  size_t offset;

  if (msg == NULL || out == NULL || out_size == NULL) {
    return false;
  }

  reason_len = (uint16_t)strnlen(msg->reason, RVNET_MAX_DISCONNECT_REASON - 1);
  offset = encode_header(out, out_cap, NET_MSG_S2C_DISCONNECT, sequence, tick);
  if (offset == 0 || !ensure_space(offset, 2 + reason_len, out_cap)) {
    return false;
  }

  write_u16_le(out + offset, reason_len);
  offset += 2;
  memcpy(out + offset, msg->reason, reason_len);
  offset += reason_len;

  *out_size = offset;
  return true;
}

bool Protocol_DecodeDisconnect(const uint8_t *data, size_t size, NetMessageHeader *out_header,
                               NetDisconnect *out_msg) {
  uint16_t reason_len;

  if (out_msg == NULL || !Protocol_ParseHeader(data, size, out_header) ||
      out_header->type != NET_MSG_S2C_DISCONNECT || size < NET_HEADER_BYTES + 2) {
    return false;
  }

  reason_len = read_u16_le(data + NET_HEADER_BYTES);
  if (reason_len >= RVNET_MAX_DISCONNECT_REASON) {
    return false;
  }
  if (size < NET_HEADER_BYTES + 2u + (size_t)reason_len) {
    return false;
  }

  memset(out_msg->reason, 0, sizeof(out_msg->reason));
  memcpy(out_msg->reason, data + NET_HEADER_BYTES + 2, reason_len);
  out_msg->reason[reason_len] = '\0';
  return true;
}
