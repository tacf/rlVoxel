# Networking Module

## Purpose

`libnet` keeps one networking model for both runtime modes:

- Internal singleplayer: client + authoritative server in-process via local in-memory transport.
- Dedicated multiplayer: client/server over ENet UDP transport.

This keeps gameplay server-authoritative in every mode.

## Architecture

Networking is split into three layers:

1. `transport` (`libnet/include/net/transport.h`)
- Packet transport only.
- Backends: ENet and local in-memory pair.

2. `protocol` (`libnet/include/net/protocol.h`)
- Wire format, versioning, message ids, encode/decode.
- Shared packet metadata helpers:
  - `Protocol_MessageTypeName(type)`
  - `Protocol_FixedPayloadSize(type)` (`0` means variable-length payload)
  - `Protocol_FixedPacketSize(type)` (`0` means variable-length packet)

3. `net` facade (`libnet/include/net/net.h`)
- Typed API for game/server code (`Net_Update`, `Net_PollEvent`, `Net_Send*`).
- Centralized routing of channel + reliability per message type.

`libnet` builds as static target `net` and is linked into `rlvoxel_runtime`.

## Runtime Modes

### Internal Singleplayer

1. Create endpoints with `Net_CreateLocalPair`.
2. Give server endpoint to `ServerCore`.
3. Keep client endpoint in `Game`.
4. Run the same protocol/events as dedicated mode, but without sockets.

### Dedicated Server

1. Server listens with `Net_Listen`.
2. Client connects with `Net_Connect`.
3. Same typed protocol flow, carried by ENet.

## Protocol v1

Every packet begins with `NetMessageHeader`:

- `magic` (`RVNET_MAGIC`)
- `version` (`RVNET_VERSION`)
- `type`
- `sequence`
- `tick`

Message set:

- `C2S_Hello`
- `S2C_Welcome`
- `C2S_InputCmd`
- `C2S_PlayerMove`
- `S2C_PlayerState`
- `S2C_ChunkData`
- `S2C_BlockDelta`
- `S2C_ChunkUnload`
- `S2C_Disconnect`

Packet sizing notes:

- Most v1 messages are fixed-size (`Protocol_FixedPayloadSize` / `Protocol_FixedPacketSize`).
- `S2C_Disconnect` is variable-length (reason text), so fixed-size helpers return `0`.

ENet channel policy:

- Channel `0`, reliable ordered: hello/welcome, chunk data, block deltas, unload, disconnect.
- Channel `1`, `C2S_PlayerMove`: unreliable sequenced (high-rate movement snapshots, latest wins).
- Channel `1`, `C2S_InputCmd`: reliable ordered (action edges + selected block authority).
- Channel `1`, `S2C_PlayerState`: reliable ordered (corrections + periodic sync).

`S2C_PlayerState` timeline anchors:

- `tick_id`: server simulation tick.
- `input_tick_id`: latest accepted `C2S_PlayerMove.tick_id`.

## Movement Sync (v2)

### Model

- Fixed 20 TPS simulation (`GAME_TICK_RATE`).
- Client predicts local movement every tick.
- Client sends:
  - `C2S_PlayerMove`: pose/velocity/look state.
  - `C2S_InputCmd`: action edges + selected block.
- Server validates and applies movement snapshot when valid.
- Server sends `S2C_PlayerState` for correction and periodic sync.

### Client Flow

Source: `src/game/game.c`

- Frame phase:
  - capture input
  - apply immediate look (no camera-look smoothing)
  - merge into pending tick input
- Tick phase:
  - process inbound network events first
  - predict local player
  - send `C2S_PlayerMove`
  - send `C2S_InputCmd` on semantic change (plus keepalive)
  - store prediction sample keyed by tick id
- On `S2C_PlayerState`:
  - use `input_tick_id` to match prediction timeline
  - if divergence is tiny, accept ack only
  - if divergence is meaningful, apply authoritative reset and replay unacked predicted inputs

### Server Flow

Source: `src/server/server_core.c`

- Tick phase:
  - consume latest movement snapshot (latest-wins)
  - validate movement delta/collision feasibility
  - accept or reject movement
  - run authoritative interactions/world update
  - send `S2C_PlayerState` for correction/periodic sync
- On reset/disconnect:
  - clear pending input state
  - reset last applied input tick anchor

### Why `input_tick_id` Matters

- It lets the client reconcile against the same input timeline the server acknowledged.
- Without it, reconciliation compares mismatched timelines and causes repeated rubberbanding.

### Debug Checklist

- Client/server built from same protocol version.
- `S2C_PlayerState.input_tick_id` is advancing.
- Prediction history has matching acknowledged ticks.
- Reconcile path replays unacked inputs after authoritative correction.
- Large corrections are rare and tied to genuine divergence.

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
NetPlayerMove move = {0};
GameplayInputCmd actions = {0};

Net_SendPlayerMove(endpoint, sequence++, tick, &move);
Net_SendInputCmd(endpoint, sequence++, tick, &actions);
```

Use `Net_SendPacket` only when raw pre-encoded control is intentionally needed.

### 4) Shutdown

```c
Net_Close(endpoint);
Net_Destroy(endpoint);
```

## Integration Points

- Client runtime: `src/game/game.c`
- Authoritative simulation: `src/server/server_core.c`
- Dedicated entrypoint: `src/server/server_main.c`
- Diagnostics overlay: `src/diagnostics/net_profiler.c`

## Further Reading

- Legacy movement page (short index): `docs/MOVEMENT_SYNC.md`
- Network diagnostics/profiler: `docs/NET_PROFILER.md`
