#ifndef RLVOXEL_NET_NET_H
#define RLVOXEL_NET_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "net/protocol.h"

/**
 * High-level networking facade used by game and server code.
 *
 * Design goals:
 * - Keep callsites simple (Net_Update + Net_PollEvent + typed sends).
 * - Hide transport differences (ENet sockets vs local in-memory pair).
 * - Expose typed payloads so gameplay code avoids manual decode paths.
 */

/**
 * Delivery semantics used by Net_SendPacket.
 * For normal gameplay usage prefer typed Net_Send* helpers, which route
 * reliability and channels for you.
 */
typedef enum NetReliability {
  NET_RELIABLE = 0,
  NET_UNRELIABLE = 1,
} NetReliability;

/**
 * High-level event kinds emitted by Net_PollEvent.
 */
typedef enum NetEventType {
  NET_EVENT_NONE = 0,
  NET_EVENT_CONNECTED = 1,
  NET_EVENT_DISCONNECTED = 2,
  NET_EVENT_MESSAGE = 3,
} NetEventType;

/**
 * Typed event payload returned by Net_PollEvent.
 *
 * If type == NET_EVENT_MESSAGE:
 * - message_type identifies which union member is valid.
 * - header contains parsed wire metadata (magic/version/type/sequence/tick).
 *
 * Payload memory is fully owned by NetEvent (value semantics).
 * No extra free call is required for decoded messages.
 */
typedef struct NetEvent {
  NetEventType type;
  int peer_id;
  NetMessageHeader header;
  NetMessageType message_type;
  union {
    NetHello hello;
    NetWelcome welcome;
    GameplayInputCmd input_cmd;
    AuthoritativePlayerState player_state;
    NetChunkDataOwned chunk_data;
    NetBlockDelta block_delta;
    NetChunkUnload chunk_unload;
    NetDisconnect disconnect;
  } payload;
} NetEvent;

typedef struct NetEndpoint NetEndpoint;

/**
 * Opens a client endpoint and begins connecting to host:port over ENet.
 * Returns NULL on allocation/initialization/connect setup failure.
 * Connection completion is observed via NET_EVENT_CONNECTED.
 */
NetEndpoint *Net_Connect(const char *host, uint16_t port);

/**
 * Opens a listening server endpoint over ENet.
 * max_clients is currently clamped by higher-level server logic (v1 uses one).
 * Returns NULL on allocation/initialization/bind failure.
 * New peers are surfaced as NET_EVENT_CONNECTED.
 */
NetEndpoint *Net_Listen(const char *bind_ip, uint16_t port, size_t max_clients);

/**
 * Creates an in-memory bidirectional endpoint pair for internal singleplayer.
 * out_client and out_server receive independent NetEndpoint handles.
 * No sockets or ENet packet serialization are required for the local path.
 */
bool Net_CreateLocalPair(NetEndpoint **out_client, NetEndpoint **out_server);

/**
 * Services network I/O for this endpoint.
 * timeout_ms behaves like ENet service timeout for remote mode.
 * Call once per frame/tick before polling events.
 */
void Net_Update(NetEndpoint *endpoint, uint32_t timeout_ms);

/**
 * Polls the next high-level event for this endpoint.
 * Returns false when no pending event exists.
 * Unknown or invalid packets are dropped internally and not emitted.
 */
bool Net_PollEvent(NetEndpoint *endpoint, NetEvent *out_event);

/**
 * Low-level packet send for pre-encoded payloads.
 * Prefer typed Net_Send* APIs unless you intentionally need raw control.
 */
bool Net_SendPacket(NetEndpoint *endpoint, const uint8_t *data, size_t size, uint8_t channel,
                    NetReliability reliability, int peer_id);

/**
 * Typed protocol sends with built-in v1 channel/reliability routing.
 * peer_id is currently implicit (v1 single client), so helpers route to peer 0.
 */
bool Net_SendHello(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick, const NetHello *hello);
bool Net_SendWelcome(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                     const NetWelcome *welcome);
bool Net_SendInputCmd(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                      const GameplayInputCmd *input_cmd);
bool Net_SendPlayerState(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                         const AuthoritativePlayerState *player_state);
bool Net_SendChunkData(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                       const NetChunkData *chunk_data);
bool Net_SendBlockDelta(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                        const NetBlockDelta *block_delta);
bool Net_SendChunkUnload(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                         const NetChunkUnload *chunk_unload);
bool Net_SendDisconnect(NetEndpoint *endpoint, uint32_t sequence, uint32_t tick,
                        const NetDisconnect *disconnect_msg);

/**
 * Initiates endpoint close/disconnect.
 */
void Net_Close(NetEndpoint *endpoint);

/**
 * Releases endpoint resources. If endpoint owns its underlying transport,
 * transport resources are also destroyed.
 */
void Net_Destroy(NetEndpoint *endpoint);

#endif
