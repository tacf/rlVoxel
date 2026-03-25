# Networking Module

## Purpose

`libnet` is the networking module for rlVoxel. It keeps one gameplay model for both:

- Internal singleplayer: client + authoritative server in the same process (local in-memory transport).
- Dedicated multiplayer: client/server over ENet UDP transport.

This lets us keep server authority in all modes, while still supporting an easy local path.

## Design

Networking is split into three layers:

1. `transport` (`libnet/include/net/transport.h`)
- Packet transport only.
- Backends: ENet and local in-memory pair.
2. `protocol` (`libnet/include/net/protocol.h`)
- Wire format, versioning, message ids, encode/decode.
3. `net` facade (`libnet/include/net/net.h`)
- Typed API for game/server code (`Net_Update`, `Net_PollEvent`, `Net_Send*`).
- Hides channel/reliability details for common messages.


`libnet` builds as a standalone static target (`net`) and is linked into `rlvoxel_runtime`.

## Runtime Modes

### Internal Singleplayer

1. Create local endpoint pair with `Net_CreateLocalPair`.
2. Pass server endpoint to `ServerCore`.
3. Keep client endpoint in `Game`.
4. Both sides run the same protocol/events without sockets.

### Dedicated Server

1. Server creates listener with `Net_Listen`.
2. Client connects with `Net_Connect`.
3. Events and typed messages flow through ENet channels.

## Protocol v1

All packets include `NetMessageHeader`:

- `magic` (`RVNET_MAGIC`)
- `version` (`RVNET_VERSION`)
- `type`
- `sequence`
- `tick`

Message set:

- `C2S_Hello`
- `S2C_Welcome`
- `C2S_InputCmd`
- `S2C_PlayerState`
- `S2C_ChunkData`
- `S2C_BlockDelta`
- `S2C_ChunkUnload`
- `S2C_Disconnect`

ENet channel policy:

- Channel `0`, reliable ordered: hello/welcome, chunk data, block deltas, unload, disconnect.
- Channel `1`, unreliable sequenced: input commands and player snapshots.

## API Usage

### 1) Create endpoint

```c
NetEndpoint *client = Net_Connect("127.0.0.1", 25565);
NetEndpoint *server = Net_Listen("*", 25565, 1);
```

Or local pair:

```c
NetEndpoint *client = NULL;
NetEndpoint *server = NULL;
Net_CreateLocalPair(&client, &server);
```

### 2) Service and poll

```c
Net_Update(endpoint, 0);

NetEvent evt;
while (Net_PollEvent(endpoint, &evt)) {
  if (evt.type == NET_EVENT_CONNECTED) { /* ... */ }
  if (evt.type == NET_EVENT_DISCONNECTED) { /* ... */ }
  if (evt.type == NET_EVENT_MESSAGE) {
    switch (evt.message_type) {
      case NET_MSG_S2C_WELCOME: /* ... */ break;
      case NET_MSG_S2C_PLAYER_STATE: /* ... */ break;
      default: break;
    }
  }
}
```

### 3) Send typed messages

```c
GameplayInputCmd cmd = {0};
Net_SendInputCmd(endpoint, sequence++, tick, &cmd);
```

Use `Net_SendPacket` only when you intentionally need raw pre-encoded payload control.

### 4) Shutdown

```c
Net_Close(endpoint);
Net_Destroy(endpoint);
```

## Integration Points

- Client runtime: `src/game/game.c`
- Authoritative simulation: `src/server/server_core.c`
- Dedicated entrypoint: `src/server/server_main.c`