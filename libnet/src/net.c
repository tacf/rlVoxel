#include "net/net.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "net/protocol.h"
#include "net/transport.h"

struct NetEndpoint {
  ITransportEndpoint *transport;
  bool owns_transport;
};

typedef bool (*NetDecodeFn)(const uint8_t *data, size_t size, NetMessageHeader *header,
                            NetEvent *event);

typedef struct NetDecodeEntry {
  NetMessageType type;
  NetDecodeFn decode_fn;
} NetDecodeEntry;

static bool net_decode_hello(const uint8_t *data, size_t size, NetMessageHeader *header,
                             NetEvent *event) {
  return Protocol_DecodeHello(data, size, header, &event->payload.hello);
}

static bool net_decode_welcome(const uint8_t *data, size_t size, NetMessageHeader *header,
                               NetEvent *event) {
  return Protocol_DecodeWelcome(data, size, header, &event->payload.welcome);
}

static bool net_decode_input_cmd(const uint8_t *data, size_t size, NetMessageHeader *header,
                                 NetEvent *event) {
  return Protocol_DecodeInputCmd(data, size, header, &event->payload.input_cmd);
}

static bool net_decode_player_move(const uint8_t *data, size_t size, NetMessageHeader *header,
                                   NetEvent *event) {
  return Protocol_DecodePlayerMove(data, size, header, &event->payload.player_move);
}

static bool net_decode_player_state(const uint8_t *data, size_t size, NetMessageHeader *header,
                                    NetEvent *event) {
  return Protocol_DecodePlayerState(data, size, header, &event->payload.player_state);
}

static bool net_decode_chunk_data(const uint8_t *data, size_t size, NetMessageHeader *header,
                                  NetEvent *event) {
  return Protocol_DecodeChunkData(data, size, header, &event->payload.chunk_data);
}

static bool net_decode_block_delta(const uint8_t *data, size_t size, NetMessageHeader *header,
                                   NetEvent *event) {
  return Protocol_DecodeBlockDelta(data, size, header, &event->payload.block_delta);
}

static bool net_decode_chunk_unload(const uint8_t *data, size_t size, NetMessageHeader *header,
                                    NetEvent *event) {
  return Protocol_DecodeChunkUnload(data, size, header, &event->payload.chunk_unload);
}

static bool net_decode_disconnect(const uint8_t *data, size_t size, NetMessageHeader *header,
                                  NetEvent *event) {
  return Protocol_DecodeDisconnect(data, size, header, &event->payload.disconnect);
}

static const NetDecodeEntry k_net_decode_table[] = {
    {NET_MSG_C2S_HELLO, net_decode_hello},
    {NET_MSG_S2C_WELCOME, net_decode_welcome},
    {NET_MSG_C2S_INPUT_CMD, net_decode_input_cmd},
    {NET_MSG_C2S_PLAYER_MOVE, net_decode_player_move},
    {NET_MSG_S2C_PLAYER_STATE, net_decode_player_state},
    {NET_MSG_S2C_CHUNK_DATA, net_decode_chunk_data},
    {NET_MSG_S2C_BLOCK_DELTA, net_decode_block_delta},
    {NET_MSG_S2C_CHUNK_UNLOAD, net_decode_chunk_unload},
    {NET_MSG_S2C_DISCONNECT, net_decode_disconnect},
};

static NetEndpoint *net_wrap_transport(ITransportEndpoint *transport, bool owns_transport) {
  NetEndpoint *endpoint;
  if (transport == NULL) {
    return NULL;
  }

  endpoint = (NetEndpoint *)calloc(1, sizeof(NetEndpoint));
  if (endpoint == NULL) {
    if (owns_transport) {
      Transport_Destroy(transport);
    }
    return NULL;
  }

  endpoint->transport = transport;
  endpoint->owns_transport = owns_transport;
  return endpoint;
}

static void net_message_route(NetMessageType message_type, uint8_t *out_channel,
                              NetReliability *out_reliability) {
  if (out_channel == NULL || out_reliability == NULL) {
    return;
  }

  if (message_type == NET_MSG_C2S_PLAYER_MOVE) {
    *out_channel = 1u;
    *out_reliability = NET_UNRELIABLE;
  } else if (message_type == NET_MSG_C2S_INPUT_CMD || message_type == NET_MSG_S2C_PLAYER_STATE) {
    *out_channel = 1u;
    *out_reliability = NET_RELIABLE;
  } else {
    *out_channel = 0u;
    *out_reliability = NET_RELIABLE;
  }
}

static bool net_decode_packet(const TransportEvent *event, NetEvent *out_event) {
  NetMessageHeader header;

  if (event == NULL || out_event == NULL || event->type != TRANSPORT_EVENT_PACKET ||
      event->data == NULL || event->size == 0) {
    return false;
  }

  if (!Protocol_ParseHeader(event->data, event->size, &header)) {
    return false;
  }

  memset(out_event, 0, sizeof(*out_event));
  out_event->type = NET_EVENT_MESSAGE;
  out_event->peer_id = event->peer_id;
  out_event->header = header;
  out_event->message_type = (NetMessageType)header.type;
  out_event->channel = event->channel;
  out_event->packet_size = event->size;

  for (size_t i = 0; i < (sizeof(k_net_decode_table) / sizeof(k_net_decode_table[0])); i++) {
    if (k_net_decode_table[i].type == (NetMessageType)header.type) {
      return k_net_decode_table[i].decode_fn(event->data, event->size, &out_event->header,
                                             out_event);
    }
  }

  return false;
}

NetEndpoint *Net_Connect(const char *host, uint16_t port) {
  ITransportEndpoint *transport = Transport_CreateEnetClient(host, port);
  return net_wrap_transport(transport, true);
}

NetEndpoint *Net_Listen(const char *bind_ip, uint16_t port, size_t max_clients) {
  ITransportEndpoint *transport = Transport_CreateEnetServer(bind_ip, port, max_clients);
  return net_wrap_transport(transport, true);
}

bool Net_CreateLocalPair(NetEndpoint **out_client, NetEndpoint **out_server) {
  ITransportEndpoint *client_transport;
  ITransportEndpoint *server_transport;
  NetEndpoint *client_endpoint;
  NetEndpoint *server_endpoint;

  if (out_client == NULL || out_server == NULL) {
    return false;
  }

  *out_client = NULL;
  *out_server = NULL;

  if (!Transport_CreateLocalPair(&client_transport, &server_transport)) {
    return false;
  }

  client_endpoint = net_wrap_transport(client_transport, true);
  if (client_endpoint == NULL) {
    Transport_Destroy(server_transport);
    return false;
  }

  server_endpoint = net_wrap_transport(server_transport, true);
  if (server_endpoint == NULL) {
    Net_Destroy(client_endpoint);
    return false;
  }

  *out_client = client_endpoint;
  *out_server = server_endpoint;
  return true;
}

void Net_Update(NetEndpoint *endpoint, uint32_t timeout_ms) {
  if (endpoint == NULL || endpoint->transport == NULL) {
    return;
  }
  Transport_Service(endpoint->transport, timeout_ms);
}

bool Net_PollEvent(NetEndpoint *endpoint, NetEvent *out_event) {
  TransportEvent transport_event;

  if (endpoint == NULL || endpoint->transport == NULL || out_event == NULL) {
    return false;
  }

  memset(out_event, 0, sizeof(*out_event));

  while (Transport_Poll(endpoint->transport, &transport_event)) {
    if (transport_event.type == TRANSPORT_EVENT_CONNECTED) {
      out_event->type = NET_EVENT_CONNECTED;
      out_event->peer_id = transport_event.peer_id;
      Transport_FreeEvent(&transport_event);
      return true;
    }

    if (transport_event.type == TRANSPORT_EVENT_DISCONNECTED) {
      out_event->type = NET_EVENT_DISCONNECTED;
      out_event->peer_id = transport_event.peer_id;
      Transport_FreeEvent(&transport_event);
      return true;
    }

    if (transport_event.type == TRANSPORT_EVENT_PACKET &&
        net_decode_packet(&transport_event, out_event)) {
      Transport_FreeEvent(&transport_event);
      return true;
    }

    Transport_FreeEvent(&transport_event);
  }

  return false;
}

bool Net_SendPacket(NetEndpoint *endpoint, const uint8_t *data, size_t size, uint8_t channel,
                    NetReliability reliability, int peer_id) {
  TransportReliability transport_reliability =
      (reliability == NET_RELIABLE) ? TRANSPORT_RELIABLE : TRANSPORT_UNRELIABLE;

  if (endpoint == NULL || endpoint->transport == NULL) {
    return false;
  }

  return Transport_Send(endpoint->transport, data, size, channel, transport_reliability, peer_id);
}

bool Net_SendHello(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick, const NetHello *hello) {
  uint8_t buffer[64];
  size_t size = 0;
  uint8_t channel = 0;
  NetReliability reliability = NET_RELIABLE;

  if (endpoint == NULL || hello == NULL) {
    return false;
  }

  if (!Protocol_EncodeHello(sequence, tick, hello, buffer, sizeof(buffer), &size)) {
    return false;
  }

  net_message_route(NET_MSG_C2S_HELLO, &channel, &reliability);
  return Net_SendPacket(endpoint, buffer, size, channel, reliability, 0);
}

bool Net_SendWelcome(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                     const NetWelcome *welcome) {
  uint8_t buffer[64];
  size_t size = 0;
  uint8_t channel = 0;
  NetReliability reliability = NET_RELIABLE;

  if (endpoint == NULL || welcome == NULL) {
    return false;
  }

  if (!Protocol_EncodeWelcome(sequence, tick, welcome, buffer, sizeof(buffer), &size)) {
    return false;
  }

  net_message_route(NET_MSG_S2C_WELCOME, &channel, &reliability);
  return Net_SendPacket(endpoint, buffer, size, channel, reliability, 0);
}

bool Net_SendInputCmd(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                      const GameplayInputCmd *input_cmd) {
  uint8_t buffer[128];
  size_t size = 0;
  uint8_t channel = 1;
  NetReliability reliability = NET_RELIABLE;

  if (endpoint == NULL || input_cmd == NULL) {
    return false;
  }

  if (!Protocol_EncodeInputCmd(sequence, tick, input_cmd, buffer, sizeof(buffer), &size)) {
    return false;
  }

  net_message_route(NET_MSG_C2S_INPUT_CMD, &channel, &reliability);
  return Net_SendPacket(endpoint, buffer, size, channel, reliability, 0);
}

bool Net_SendPlayerMove(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                        const NetPlayerMove *player_move) {
  uint8_t buffer[128];
  size_t size = 0;
  uint8_t channel = 1;
  NetReliability reliability = NET_UNRELIABLE;

  if (endpoint == NULL || player_move == NULL) {
    return false;
  }

  if (!Protocol_EncodePlayerMove(sequence, tick, player_move, buffer, sizeof(buffer), &size)) {
    return false;
  }

  net_message_route(NET_MSG_C2S_PLAYER_MOVE, &channel, &reliability);
  return Net_SendPacket(endpoint, buffer, size, channel, reliability, 0);
}

bool Net_SendPlayerState(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                         const AuthoritativePlayerState *player_state) {
  uint8_t buffer[128];
  size_t size = 0;
  uint8_t channel = 1;
  NetReliability reliability = NET_RELIABLE;

  if (endpoint == NULL || player_state == NULL) {
    return false;
  }

  if (!Protocol_EncodePlayerState(sequence, tick, player_state, buffer, sizeof(buffer), &size)) {
    return false;
  }

  net_message_route(NET_MSG_S2C_PLAYER_STATE, &channel, &reliability);
  return Net_SendPacket(endpoint, buffer, size, channel, reliability, 0);
}

bool Net_SendChunkData(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                       const NetChunkData *chunk_data) {
  uint8_t *buffer;
  size_t size = 0;
  size_t capacity;
  bool ok;
  uint8_t channel = 0;
  NetReliability reliability = NET_RELIABLE;

  if (endpoint == NULL || chunk_data == NULL) {
    return false;
  }

  capacity = Protocol_HeaderSize() + 8 + WORLD_CHUNK_VOLUME + WORLD_CHUNK_LIGHT_BYTES +
             (WORLD_CHUNK_SIZE_X * WORLD_CHUNK_SIZE_Z);
  buffer = (uint8_t *)malloc(capacity);
  if (buffer == NULL) {
    return false;
  }

  ok = Protocol_EncodeChunkData(sequence, tick, chunk_data, buffer, capacity, &size);
  if (!ok) {
    free(buffer);
    return false;
  }

  net_message_route(NET_MSG_S2C_CHUNK_DATA, &channel, &reliability);
  ok = Net_SendPacket(endpoint, buffer, size, channel, reliability, 0);
  free(buffer);
  return ok;
}

bool Net_SendBlockDelta(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                        const NetBlockDelta *block_delta) {
  uint8_t buffer[64];
  size_t size = 0;
  uint8_t channel = 0;
  NetReliability reliability = NET_RELIABLE;

  if (endpoint == NULL || block_delta == NULL) {
    return false;
  }

  if (!Protocol_EncodeBlockDelta(sequence, tick, block_delta, buffer, sizeof(buffer), &size)) {
    return false;
  }

  net_message_route(NET_MSG_S2C_BLOCK_DELTA, &channel, &reliability);
  return Net_SendPacket(endpoint, buffer, size, channel, reliability, 0);
}

bool Net_SendChunkUnload(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                         const NetChunkUnload *chunk_unload) {
  uint8_t buffer[64];
  size_t size = 0;
  uint8_t channel = 0;
  NetReliability reliability = NET_RELIABLE;

  if (endpoint == NULL || chunk_unload == NULL) {
    return false;
  }

  if (!Protocol_EncodeChunkUnload(sequence, tick, chunk_unload, buffer, sizeof(buffer), &size)) {
    return false;
  }

  net_message_route(NET_MSG_S2C_CHUNK_UNLOAD, &channel, &reliability);
  return Net_SendPacket(endpoint, buffer, size, channel, reliability, 0);
}

bool Net_SendDisconnect(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                        const NetDisconnect *disconnect_msg) {
  uint8_t buffer[256];
  size_t size = 0;
  uint8_t channel = 0;
  NetReliability reliability = NET_RELIABLE;

  if (endpoint == NULL || disconnect_msg == NULL) {
    return false;
  }

  if (!Protocol_EncodeDisconnect(sequence, tick, disconnect_msg, buffer, sizeof(buffer), &size)) {
    return false;
  }

  net_message_route(NET_MSG_S2C_DISCONNECT, &channel, &reliability);
  return Net_SendPacket(endpoint, buffer, size, channel, reliability, 0);
}

void Net_Close(NetEndpoint *endpoint) {
  if (endpoint == NULL || endpoint->transport == NULL) {
    return;
  }
  Transport_Close(endpoint->transport);
}

void Net_Destroy(NetEndpoint *endpoint) {
  if (endpoint == NULL) {
    return;
  }

  if (endpoint->transport != NULL && endpoint->owns_transport) {
    Transport_Destroy(endpoint->transport);
  }
  endpoint->transport = NULL;
  endpoint->owns_transport = false;
  free(endpoint);
}
