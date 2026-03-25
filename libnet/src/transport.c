#include "net/transport.h"

#include <enet/enet.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_QUEUE_CAPACITY 1024

typedef struct EventQueueEntry {
  TransportEventType type;
  int peer_id;
  uint8_t channel;
  uint8_t *data;
  size_t size;
} EventQueueEntry;

typedef struct EventQueue {
  EventQueueEntry entries[EVENT_QUEUE_CAPACITY];
  size_t head;
  size_t tail;
  size_t count;
  pthread_mutex_t mutex;
} EventQueue;

static bool event_queue_init(EventQueue *queue) {
  if (queue == NULL) {
    return false;
  }

  memset(queue, 0, sizeof(*queue));
  return pthread_mutex_init(&queue->mutex, NULL) == 0;
}

static void event_queue_shutdown(EventQueue *queue) {
  size_t i;

  if (queue == NULL) {
    return;
  }

  pthread_mutex_lock(&queue->mutex);
  for (i = 0; i < queue->count; i++) {
    size_t idx = (queue->head + i) % EVENT_QUEUE_CAPACITY;
    free(queue->entries[idx].data);
    queue->entries[idx].data = NULL;
    queue->entries[idx].size = 0;
  }
  queue->head = 0;
  queue->tail = 0;
  queue->count = 0;
  pthread_mutex_unlock(&queue->mutex);

  pthread_mutex_destroy(&queue->mutex);
}

static bool event_queue_push(EventQueue *queue, const EventQueueEntry *entry) {
  EventQueueEntry *slot;

  if (queue == NULL || entry == NULL) {
    return false;
  }

  pthread_mutex_lock(&queue->mutex);
  if (queue->count >= EVENT_QUEUE_CAPACITY) {
    pthread_mutex_unlock(&queue->mutex);
    return false;
  }

  slot = &queue->entries[queue->tail];
  *slot = *entry;
  queue->tail = (queue->tail + 1) % EVENT_QUEUE_CAPACITY;
  queue->count++;
  pthread_mutex_unlock(&queue->mutex);
  return true;
}

static bool event_queue_push_copy(EventQueue *queue, TransportEventType type, int peer_id,
                                  uint8_t channel, const uint8_t *data, size_t size) {
  EventQueueEntry entry;

  memset(&entry, 0, sizeof(entry));
  entry.type = type;
  entry.peer_id = peer_id;
  entry.channel = channel;

  if (data != NULL && size > 0) {
    entry.data = (uint8_t *)malloc(size);
    if (entry.data == NULL) {
      return false;
    }
    memcpy(entry.data, data, size);
    entry.size = size;
  }

  if (!event_queue_push(queue, &entry)) {
    free(entry.data);
    return false;
  }

  return true;
}

static bool event_queue_pop(EventQueue *queue, TransportEvent *out_event) {
  EventQueueEntry entry;

  if (queue == NULL || out_event == NULL) {
    return false;
  }

  pthread_mutex_lock(&queue->mutex);
  if (queue->count == 0) {
    pthread_mutex_unlock(&queue->mutex);
    return false;
  }

  entry = queue->entries[queue->head];
  memset(&queue->entries[queue->head], 0, sizeof(queue->entries[queue->head]));
  queue->head = (queue->head + 1) % EVENT_QUEUE_CAPACITY;
  queue->count--;
  pthread_mutex_unlock(&queue->mutex);

  out_event->type = entry.type;
  out_event->peer_id = entry.peer_id;
  out_event->channel = entry.channel;
  out_event->data = entry.data;
  out_event->size = entry.size;
  return true;
}

typedef struct LocalTransportShared {
  EventQueue client_inbox;
  EventQueue server_inbox;
  pthread_mutex_t ref_mutex;
  int ref_count;
} LocalTransportShared;

typedef struct LocalTransportEndpoint {
  LocalTransportShared *shared;
  EventQueue *inbox;
  EventQueue *outbox;
  bool closed;
} LocalTransportEndpoint;

static void local_shared_retain(LocalTransportShared *shared) {
  pthread_mutex_lock(&shared->ref_mutex);
  shared->ref_count++;
  pthread_mutex_unlock(&shared->ref_mutex);
}

static void local_shared_release(LocalTransportShared *shared) {
  int should_free = 0;

  pthread_mutex_lock(&shared->ref_mutex);
  shared->ref_count--;
  if (shared->ref_count <= 0) {
    should_free = 1;
  }
  pthread_mutex_unlock(&shared->ref_mutex);

  if (!should_free) {
    return;
  }

  event_queue_shutdown(&shared->client_inbox);
  event_queue_shutdown(&shared->server_inbox);
  pthread_mutex_destroy(&shared->ref_mutex);
  free(shared);
}

static bool local_send(ITransportEndpoint *endpoint, const uint8_t *data, size_t size,
                       uint8_t channel, TransportReliability reliability, int peer_id) {
  LocalTransportEndpoint *local = (LocalTransportEndpoint *)endpoint->impl;
  (void)reliability;
  (void)peer_id;

  if (local == NULL || local->closed || data == NULL || size == 0) {
    return false;
  }

  return event_queue_push_copy(local->outbox, TRANSPORT_EVENT_PACKET, 0, channel, data, size);
}

static bool local_poll(ITransportEndpoint *endpoint, TransportEvent *out_event) {
  LocalTransportEndpoint *local = (LocalTransportEndpoint *)endpoint->impl;
  if (local == NULL) {
    return false;
  }

  return event_queue_pop(local->inbox, out_event);
}

static void local_service(ITransportEndpoint *endpoint, uint32_t timeout_ms) {
  (void)endpoint;
  (void)timeout_ms;
}

static void local_close(ITransportEndpoint *endpoint) {
  LocalTransportEndpoint *local = (LocalTransportEndpoint *)endpoint->impl;
  if (local == NULL || local->closed) {
    return;
  }

  local->closed = true;
  event_queue_push_copy(local->outbox, TRANSPORT_EVENT_DISCONNECTED, 0, 0, NULL, 0);
  event_queue_push_copy(local->inbox, TRANSPORT_EVENT_DISCONNECTED, 0, 0, NULL, 0);
}

static void local_destroy(ITransportEndpoint *endpoint) {
  LocalTransportEndpoint *local;

  if (endpoint == NULL) {
    return;
  }

  local = (LocalTransportEndpoint *)endpoint->impl;
  if (local != NULL) {
    local_close(endpoint);
    local_shared_release(local->shared);
    free(local);
    endpoint->impl = NULL;
  }

  free(endpoint);
}

static pthread_mutex_t g_enet_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_enet_ref_count = 0;

static bool enet_ref_init(void) {
  bool ok = true;

  pthread_mutex_lock(&g_enet_mutex);
  if (g_enet_ref_count == 0) {
    if (enet_initialize() != 0) {
      ok = false;
    }
  }
  if (ok) {
    g_enet_ref_count++;
  }
  pthread_mutex_unlock(&g_enet_mutex);
  return ok;
}

static void enet_ref_shutdown(void) {
  pthread_mutex_lock(&g_enet_mutex);
  g_enet_ref_count--;
  if (g_enet_ref_count <= 0) {
    g_enet_ref_count = 0;
    enet_deinitialize();
  }
  pthread_mutex_unlock(&g_enet_mutex);
}

typedef struct EnetTransportEndpoint {
  ENetHost *host;
  ENetPeer *peer;
  bool is_server;
  bool closed;
  EventQueue inbox;
} EnetTransportEndpoint;

static bool enet_send(ITransportEndpoint *endpoint, const uint8_t *data, size_t size,
                      uint8_t channel, TransportReliability reliability, int peer_id) {
  EnetTransportEndpoint *enet;
  ENetPacket *packet;
  enet_uint32 flags = 0;

  (void)peer_id;

  if (endpoint == NULL || endpoint->impl == NULL || data == NULL || size == 0) {
    return false;
  }

  enet = (EnetTransportEndpoint *)endpoint->impl;
  if (enet->closed || enet->peer == NULL) {
    return false;
  }

  if (reliability == TRANSPORT_RELIABLE) {
    flags |= ENET_PACKET_FLAG_RELIABLE;
  }

  packet = enet_packet_create(data, size, flags);
  if (packet == NULL) {
    return false;
  }

  if (enet_peer_send(enet->peer, channel, packet) != 0) {
    enet_packet_destroy(packet);
    return false;
  }

  enet_host_flush(enet->host);
  return true;
}

static bool enet_poll(ITransportEndpoint *endpoint, TransportEvent *out_event) {
  EnetTransportEndpoint *enet = (EnetTransportEndpoint *)endpoint->impl;
  if (enet == NULL || out_event == NULL) {
    return false;
  }

  return event_queue_pop(&enet->inbox, out_event);
}

static void enet_handle_event(EnetTransportEndpoint *enet, const ENetEvent *event) {
  if (enet == NULL || event == NULL) {
    return;
  }

  switch (event->type) {
  case ENET_EVENT_TYPE_CONNECT:
    if (enet->is_server && enet->peer != NULL && enet->peer != event->peer) {
      enet_peer_disconnect_later(event->peer, 0);
      break;
    }
    enet->peer = event->peer;
    event_queue_push_copy(&enet->inbox, TRANSPORT_EVENT_CONNECTED, 0, 0, NULL, 0);
    break;

  case ENET_EVENT_TYPE_RECEIVE:
    if (event->packet != NULL) {
      event_queue_push_copy(&enet->inbox, TRANSPORT_EVENT_PACKET, 0, event->channelID,
                            event->packet->data, event->packet->dataLength);
      enet_packet_destroy(event->packet);
    }
    break;

  case ENET_EVENT_TYPE_DISCONNECT:
    if (enet->peer == event->peer) {
      enet->peer = NULL;
    }
    event_queue_push_copy(&enet->inbox, TRANSPORT_EVENT_DISCONNECTED, 0, 0, NULL, 0);
    break;

  default:
    break;
  }
}

static void enet_service(ITransportEndpoint *endpoint, uint32_t timeout_ms) {
  EnetTransportEndpoint *enet;
  ENetEvent event;
  int serviced;

  if (endpoint == NULL || endpoint->impl == NULL) {
    return;
  }

  enet = (EnetTransportEndpoint *)endpoint->impl;
  if (enet->closed || enet->host == NULL) {
    return;
  }

  serviced = enet_host_service(enet->host, &event, timeout_ms);
  if (serviced > 0) {
    enet_handle_event(enet, &event);
  }

  while (enet_host_service(enet->host, &event, 0) > 0) {
    enet_handle_event(enet, &event);
  }
}

static void enet_close(ITransportEndpoint *endpoint) {
  EnetTransportEndpoint *enet;

  if (endpoint == NULL || endpoint->impl == NULL) {
    return;
  }

  enet = (EnetTransportEndpoint *)endpoint->impl;
  if (enet->closed) {
    return;
  }

  enet->closed = true;
  if (enet->peer != NULL) {
    enet_peer_disconnect(enet->peer, 0);
    enet_host_flush(enet->host);
    enet->peer = NULL;
  }
}

static void enet_destroy(ITransportEndpoint *endpoint) {
  EnetTransportEndpoint *enet;

  if (endpoint == NULL) {
    return;
  }

  enet = (EnetTransportEndpoint *)endpoint->impl;
  if (enet != NULL) {
    enet_close(endpoint);
    event_queue_shutdown(&enet->inbox);
    if (enet->host != NULL) {
      enet_host_destroy(enet->host);
      enet->host = NULL;
    }
    enet_ref_shutdown();
    free(enet);
    endpoint->impl = NULL;
  }

  free(endpoint);
}

bool Transport_Send(ITransportEndpoint *endpoint, const uint8_t *data, size_t size, uint8_t channel,
                    TransportReliability reliability, int peer_id) {
  if (endpoint == NULL || endpoint->send_fn == NULL) {
    return false;
  }
  return endpoint->send_fn(endpoint, data, size, channel, reliability, peer_id);
}

bool Transport_Poll(ITransportEndpoint *endpoint, TransportEvent *out_event) {
  if (out_event == NULL) {
    return false;
  }

  memset(out_event, 0, sizeof(*out_event));
  if (endpoint == NULL || endpoint->poll_fn == NULL) {
    return false;
  }

  return endpoint->poll_fn(endpoint, out_event);
}

void Transport_Service(ITransportEndpoint *endpoint, uint32_t timeout_ms) {
  if (endpoint == NULL || endpoint->service_fn == NULL) {
    return;
  }

  endpoint->service_fn(endpoint, timeout_ms);
}

void Transport_Close(ITransportEndpoint *endpoint) {
  if (endpoint == NULL || endpoint->close_fn == NULL) {
    return;
  }

  endpoint->close_fn(endpoint);
}

void Transport_Destroy(ITransportEndpoint *endpoint) {
  if (endpoint == NULL || endpoint->destroy_fn == NULL) {
    return;
  }

  endpoint->destroy_fn(endpoint);
}

void Transport_FreeEvent(TransportEvent *event) {
  if (event == NULL) {
    return;
  }

  free(event->data);
  memset(event, 0, sizeof(*event));
}

bool Transport_CreateLocalPair(ITransportEndpoint **out_client, ITransportEndpoint **out_server) {
  LocalTransportShared *shared;
  LocalTransportEndpoint *client_impl;
  LocalTransportEndpoint *server_impl;
  ITransportEndpoint *client;
  ITransportEndpoint *server;
  bool client_inbox_ok = false;
  bool server_inbox_ok = false;
  bool ref_mutex_ok = false;

  if (out_client == NULL || out_server == NULL) {
    return false;
  }

  *out_client = NULL;
  *out_server = NULL;

  shared = (LocalTransportShared *)calloc(1, sizeof(LocalTransportShared));
  if (shared == NULL) {
    return false;
  }

  client_inbox_ok = event_queue_init(&shared->client_inbox);
  if (client_inbox_ok) {
    server_inbox_ok = event_queue_init(&shared->server_inbox);
  }
  if (client_inbox_ok && server_inbox_ok) {
    ref_mutex_ok = (pthread_mutex_init(&shared->ref_mutex, NULL) == 0);
  }
  if (!client_inbox_ok || !server_inbox_ok || !ref_mutex_ok) {
    if (server_inbox_ok) {
      event_queue_shutdown(&shared->server_inbox);
    }
    if (client_inbox_ok) {
      event_queue_shutdown(&shared->client_inbox);
    }
    if (ref_mutex_ok) {
      pthread_mutex_destroy(&shared->ref_mutex);
    }
    free(shared);
    return false;
  }
  shared->ref_count = 0;

  client = (ITransportEndpoint *)calloc(1, sizeof(ITransportEndpoint));
  server = (ITransportEndpoint *)calloc(1, sizeof(ITransportEndpoint));
  client_impl = (LocalTransportEndpoint *)calloc(1, sizeof(LocalTransportEndpoint));
  server_impl = (LocalTransportEndpoint *)calloc(1, sizeof(LocalTransportEndpoint));

  if (client == NULL || server == NULL || client_impl == NULL || server_impl == NULL) {
    free(client);
    free(server);
    free(client_impl);
    free(server_impl);
    local_shared_release(shared);
    return false;
  }

  client_impl->shared = shared;
  client_impl->inbox = &shared->client_inbox;
  client_impl->outbox = &shared->server_inbox;
  server_impl->shared = shared;
  server_impl->inbox = &shared->server_inbox;
  server_impl->outbox = &shared->client_inbox;

  local_shared_retain(shared);
  local_shared_retain(shared);

  client->impl = client_impl;
  client->send_fn = local_send;
  client->poll_fn = local_poll;
  client->service_fn = local_service;
  client->close_fn = local_close;
  client->destroy_fn = local_destroy;

  server->impl = server_impl;
  server->send_fn = local_send;
  server->poll_fn = local_poll;
  server->service_fn = local_service;
  server->close_fn = local_close;
  server->destroy_fn = local_destroy;

  event_queue_push_copy(client_impl->inbox, TRANSPORT_EVENT_CONNECTED, 0, 0, NULL, 0);
  event_queue_push_copy(server_impl->inbox, TRANSPORT_EVENT_CONNECTED, 0, 0, NULL, 0);

  *out_client = client;
  *out_server = server;
  return true;
}

ITransportEndpoint *Transport_CreateEnetClient(const char *host, uint16_t port) {
  ITransportEndpoint *endpoint;
  EnetTransportEndpoint *impl;
  ENetAddress address;

  if (host == NULL || !enet_ref_init()) {
    return NULL;
  }

  endpoint = (ITransportEndpoint *)calloc(1, sizeof(ITransportEndpoint));
  impl = (EnetTransportEndpoint *)calloc(1, sizeof(EnetTransportEndpoint));
  if (endpoint == NULL || impl == NULL) {
    free(endpoint);
    free(impl);
    enet_ref_shutdown();
    return NULL;
  }

  if (!event_queue_init(&impl->inbox)) {
    free(endpoint);
    free(impl);
    enet_ref_shutdown();
    return NULL;
  }

  impl->host = enet_host_create(NULL, 1, 2, 0, 0);
  if (impl->host == NULL) {
    event_queue_shutdown(&impl->inbox);
    free(endpoint);
    free(impl);
    enet_ref_shutdown();
    return NULL;
  }

  if (enet_address_set_host(&address, host) != 0) {
    enet_host_destroy(impl->host);
    event_queue_shutdown(&impl->inbox);
    free(endpoint);
    free(impl);
    enet_ref_shutdown();
    return NULL;
  }
  address.port = port;
  impl->peer = enet_host_connect(impl->host, &address, 2, 0);
  if (impl->peer == NULL) {
    enet_host_destroy(impl->host);
    event_queue_shutdown(&impl->inbox);
    free(endpoint);
    free(impl);
    enet_ref_shutdown();
    return NULL;
  }

  endpoint->impl = impl;
  endpoint->send_fn = enet_send;
  endpoint->poll_fn = enet_poll;
  endpoint->service_fn = enet_service;
  endpoint->close_fn = enet_close;
  endpoint->destroy_fn = enet_destroy;
  return endpoint;
}

ITransportEndpoint *Transport_CreateEnetServer(const char *bind_ip, uint16_t port,
                                               size_t max_clients) {
  ITransportEndpoint *endpoint;
  EnetTransportEndpoint *impl;
  ENetAddress address;

  if (!enet_ref_init()) {
    return NULL;
  }

  endpoint = (ITransportEndpoint *)calloc(1, sizeof(ITransportEndpoint));
  impl = (EnetTransportEndpoint *)calloc(1, sizeof(EnetTransportEndpoint));
  if (endpoint == NULL || impl == NULL) {
    free(endpoint);
    free(impl);
    enet_ref_shutdown();
    return NULL;
  }

  if (!event_queue_init(&impl->inbox)) {
    free(endpoint);
    free(impl);
    enet_ref_shutdown();
    return NULL;
  }

  memset(&address, 0, sizeof(address));
  address.port = port;

  if (bind_ip == NULL || bind_ip[0] == '\0' || strcmp(bind_ip, "*") == 0) {
    address.host = ENET_HOST_ANY;
  } else {
    if (enet_address_set_host(&address, bind_ip) != 0) {
      event_queue_shutdown(&impl->inbox);
      free(endpoint);
      free(impl);
      enet_ref_shutdown();
      return NULL;
    }
  }

  impl->is_server = true;
  impl->host = enet_host_create(&address, max_clients > 0 ? max_clients : 1, 2, 0, 0);
  if (impl->host == NULL) {
    event_queue_shutdown(&impl->inbox);
    free(endpoint);
    free(impl);
    enet_ref_shutdown();
    return NULL;
  }

  endpoint->impl = impl;
  endpoint->send_fn = enet_send;
  endpoint->poll_fn = enet_poll;
  endpoint->service_fn = enet_service;
  endpoint->close_fn = enet_close;
  endpoint->destroy_fn = enet_destroy;
  return endpoint;
}
