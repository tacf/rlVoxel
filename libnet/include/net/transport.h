#ifndef RLVOXEL_NET_TRANSPORT_H
#define RLVOXEL_NET_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Low-level transport abstraction.
 *
 * The transport layer is intentionally packet-oriented and message-agnostic.
 * It is used by the higher-level Net facade to support:
 * - ENet remote client/server endpoints
 * - local in-memory endpoint pairs for internal singleplayer
 */

/** Delivery semantics for transport-level packet sends. */
typedef enum TransportReliability {
  TRANSPORT_RELIABLE = 0,
  TRANSPORT_UNRELIABLE = 1,
} TransportReliability;

/** Raw transport event categories. */
typedef enum TransportEventType {
  TRANSPORT_EVENT_NONE = 0,
  TRANSPORT_EVENT_CONNECTED = 1,
  TRANSPORT_EVENT_DISCONNECTED = 2,
  TRANSPORT_EVENT_PACKET = 3,
} TransportEventType;

/** Raw transport event payload. */
typedef struct TransportEvent {
  TransportEventType type;
  int peer_id;
  uint8_t channel;
  /* Heap-owned copy for TRANSPORT_EVENT_PACKET; free with Transport_FreeEvent. */
  uint8_t *data;
  size_t size;
} TransportEvent;

typedef struct ITransportEndpoint ITransportEndpoint;

/** Virtual transport endpoint interface (ENet or local in-memory). */
struct ITransportEndpoint {
  void *impl;
  bool (*send_fn)(ITransportEndpoint *endpoint, const uint8_t *data, size_t size, uint8_t channel,
                  TransportReliability reliability, int peer_id);
  bool (*poll_fn)(ITransportEndpoint *endpoint, TransportEvent *out_event);
  void (*service_fn)(ITransportEndpoint *endpoint, uint32_t timeout_ms);
  void (*close_fn)(ITransportEndpoint *endpoint);
  void (*destroy_fn)(ITransportEndpoint *endpoint);
};

/** Sends a raw packet through endpoint transport. */
bool Transport_Send(ITransportEndpoint *endpoint, const uint8_t *data, size_t size, uint8_t channel,
                    TransportReliability reliability, int peer_id);
/** Polls next raw event from endpoint inbox. */
bool Transport_Poll(ITransportEndpoint *endpoint, TransportEvent *out_event);
/** Services transport I/O (socket pump for ENet). */
void Transport_Service(ITransportEndpoint *endpoint, uint32_t timeout_ms);
/** Initiates disconnect/close semantics on endpoint. */
void Transport_Close(ITransportEndpoint *endpoint);
/** Destroys endpoint and owned resources. */
void Transport_Destroy(ITransportEndpoint *endpoint);
/** Releases heap-owned event data buffer and zeroes struct. */
void Transport_FreeEvent(TransportEvent *event);

/** Creates local in-memory client/server endpoint pair. */
bool Transport_CreateLocalPair(ITransportEndpoint **out_client, ITransportEndpoint **out_server);
/** Creates ENet client endpoint and starts connect attempt. */
ITransportEndpoint *Transport_CreateEnetClient(const char *host, uint16_t port);
/** Creates ENet server endpoint bound to bind_ip:port. */
ITransportEndpoint *Transport_CreateEnetServer(const char *bind_ip, uint16_t port,
                                               size_t max_clients);

#endif
