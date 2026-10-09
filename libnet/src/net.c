#include "net/net.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "net/protocol.h"
#include "net/transport.h"

struct NetEndpoint {
  ITransportEndpoint *transport;
  bool owns_transport;
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

  const uint8_t *data = event->data;
  size_t size = event->size;
  switch (out_event->message_type) {
  case NET_MSG_C2S_HELLO:
    return Protocol_DecodeHello(data, size, &out_event->header, &out_event->payload.hello);
  case NET_MSG_S2C_WELCOME:
    return Protocol_DecodeWelcome(data, size, &out_event->header, &out_event->payload.welcome);
  case NET_MSG_C2S_INPUT_CMD:
    return Protocol_DecodeInputCmd(data, size, &out_event->header, &out_event->payload.input_cmd);
  case NET_MSG_C2S_PLAYER_MOVE:
    return Protocol_DecodePlayerMove(data, size, &out_event->header,
                                     &out_event->payload.player_move);
  case NET_MSG_S2C_PLAYER_STATE:
    return Protocol_DecodePlayerState(data, size, &out_event->header,
                                      &out_event->payload.player_state);
  case NET_MSG_S2C_CHUNK_DATA:
    return Protocol_DecodeChunkData(data, size, &out_event->header, &out_event->payload.chunk_data);
  case NET_MSG_S2C_BLOCK_DELTA:
    return Protocol_DecodeBlockDelta(data, size, &out_event->header,
                                     &out_event->payload.block_delta);
  case NET_MSG_S2C_CHUNK_UNLOAD:
    return Protocol_DecodeChunkUnload(data, size, &out_event->header,
                                      &out_event->payload.chunk_unload);
  case NET_MSG_S2C_DISCONNECT:
    return Protocol_DecodeDisconnect(data, size, &out_event->header,
                                     &out_event->payload.disconnect);
  default:
    return false;
  }
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

  if (!Protocol_EncodeHello(sequence, tick, hello, buffer, sizeof(buffer), &size)) {
    return false;
  }
  return Net_SendPacket(endpoint, buffer, size, 0, NET_RELIABLE, 0);
}

bool Net_SendInputCmd(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                      const GameplayInputCmd *input_cmd) {
  uint8_t buffer[128];
  size_t size = 0;

  if (!Protocol_EncodeInputCmd(sequence, tick, input_cmd, buffer, sizeof(buffer), &size)) {
    return false;
  }
  return Net_SendPacket(endpoint, buffer, size, 1, NET_RELIABLE, 0);
}

bool Net_SendPlayerMove(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                        const NetPlayerMove *player_move) {
  uint8_t buffer[128];
  size_t size = 0;

  if (!Protocol_EncodePlayerMove(sequence, tick, player_move, buffer, sizeof(buffer), &size)) {
    return false;
  }
  return Net_SendPacket(endpoint, buffer, size, 1, NET_UNRELIABLE, 0);
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
