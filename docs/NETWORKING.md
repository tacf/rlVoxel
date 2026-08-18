# Networking Module

## What this module does

`libnet` gives the game one networking system that works in both runtime modes:

- **Internal singleplayer**: client and server run inside the same process and talk through a local in-memory connection.
- **Dedicated multiplayer**: client and server talk over the network using ENet and UDP.

The goal is to keep the game **server-authoritative** in both cases: the server is always the final source of truth, even in singleplayer.

---

## Big picture

The networking code splits into three layers:

### 1. `transport` (`libnet/include/net/transport.h`)

Lowest layer. Only job is moving packets from one place to another.

Backends:

- ENet
- local in-memory transport pair

### 2. `protocol` (`libnet/include/net/protocol.h`)

Defines the wire format: message types, versioning, packet encoding/decoding, and shared packet metadata helpers.

Useful helpers:

- `Protocol_MessageTypeName(type)`
- `Protocol_FixedPayloadSize(type)`: returns `0` for variable-size payloads
- `Protocol_FixedPacketSize(type)`: returns `0` for variable-size packets

### 3. `net` facade (`libnet/include/net/net.h`)

The gameplay-facing API. Game and server code use this layer directly through functions like:

- `Net_Update`
- `Net_PollEvent`
- `Net_Send*`

It also decides which channel and reliability mode each message type uses.

---

## Build target

`libnet` builds as the static target `net` and links into `rlvoxel_runtime`.

---

## Runtime modes

## Internal singleplayer

Singleplayer still runs the full client/server flow; the only difference is both sides live in the same process.

How it works:

1. Create a local endpoint pair with `Net_CreateLocalPair`.
2. Give the server endpoint to `ServerCore`.
3. Keep the client endpoint in `Game`.
4. Run the same protocol and events used by dedicated multiplayer.

So even offline singleplayer goes through the network layer, keeping behavior consistent between modes.

## Dedicated server

In multiplayer:

1. The server starts listening with `Net_Listen`.
2. The client connects with `Net_Connect`.
3. The same typed protocol is used, but packets are carried by ENet.

---

## Protocol v2

Every packet starts with a `NetMessageHeader` containing:

- `magic` (`RVNET_MAGIC`)
- `version` (`RVNET_VERSION`)
- `type`
- `sequence`
- `tick`

### Message types

The current message set:

- `C2S_Hello`
- `S2C_Welcome`
- `C2S_InputCmd`
- `C2S_PlayerMove`
- `S2C_PlayerState`
- `S2C_ChunkData`
- `S2C_BlockDelta`
- `S2C_ChunkUnload`
- `S2C_Disconnect`

### Packet size notes

Most v2 messages are fixed size, so you can use:

- `Protocol_FixedPayloadSize(...)`
- `Protocol_FixedPacketSize(...)`

The exception is `S2C_Disconnect`: it carries a text reason, so its size varies and the fixed-size helpers return `0` for it.

---

## ENet channel rules

### Channel 0: reliable and ordered

For important messages that must arrive and stay in order:

- hello / welcome
- chunk data
- block updates
- chunk unload
- disconnect

### Channel 1: gameplay updates

For movement and player state.

- `C2S_PlayerMove`: **unreliable sequenced**.
  High-rate movement updates where newer data replaces older data.

- `C2S_InputCmd`: **reliable ordered**.
  Action presses/releases, selected block, and authority-related gameplay input.

  Also carries:
  - gameplay mode (`creative` or `survival`)
  - fly toggle state

- `S2C_PlayerState`: **reliable ordered**.
  Corrections and periodic synchronization from the server.

---

## `S2C_PlayerState` timeline fields

`S2C_PlayerState` includes two timeline anchors that matter for client prediction and reconciliation:

- `tick_id`: the current server simulation tick
- `input_tick_id`: the latest `C2S_PlayerMove.tick_id` the server accepted

---

## Movement sync (v2)

## Basic idea

Movement runs on a fixed **20 ticks per second** simulation (`GAME_TICK_RATE`).

The client predicts its own movement locally so controls feel responsive. The server stays authoritative and decides what's actually valid.

### The client sends

- `C2S_PlayerMove`: player position, velocity, look direction, and related movement state
- `C2S_InputCmd`: input actions, selected block, gameplay mode, and fly state

### The server sends

- `S2C_PlayerState`: corrections and regular sync updates

---

## Client-side flow

Source: `src/game/game.c`

### Every frame

The client:

- reads player input
- applies look changes immediately
- merges input into the pending tick state

There's **no camera-look smoothing** here.

### Every tick

The client:

1. processes incoming network events first
2. predicts local player movement
3. sends `C2S_PlayerMove`
4. sends `C2S_InputCmd` when something meaningful changes, plus occasional keepalive updates
5. stores a prediction sample keyed by the current tick id

### When `S2C_PlayerState` arrives

The client:

1. uses `input_tick_id` to find the matching point in its prediction history
2. checks whether the difference is tiny or meaningful
3. if tiny, treats the state as acknowledged and moves on
4. if meaningful, resets to the authoritative server state and replays any unacknowledged predicted inputs

Short version: predict first, correct when the server disagrees.

---

## Server-side flow

Source: `src/server/server_core.c`

### Every tick

The server:

1. uses the latest movement snapshot
2. validates movement changes and collision feasibility
3. accepts or rejects the movement
4. runs authoritative interactions and world updates
5. sends `S2C_PlayerState` back to the client for correction or sync

### On reset or disconnect

The server clears:

- pending input state
- the last applied input tick anchor

---

## Why `input_tick_id` matters

It lets the client compare its prediction against the **same input timeline** the server actually processed. Reconciliation only works correctly when both sides agree on the same moment in the input history.

Without `input_tick_id`, the client can compare against the wrong prediction sample, which shows up as repeated rubberbanding or bad corrections.

---

## Debug checklist

If movement sync feels wrong, check these first:

- Client and server are built with the same protocol version.
- `S2C_PlayerState.input_tick_id` is increasing as expected.
- The client still has matching prediction history for the acknowledged ticks.
- After a correction, the client replays unacknowledged inputs.
- Big corrections are rare and only happen when there's real divergence.

---

## Basic API usage

## 1. Create an endpoint

Dedicated mode:

```c
NetEndpoint *client = Net_Connect("127.0.0.1", 25565);
NetEndpoint *server = Net_Listen("*", 25565, 1);
```

Local pair:

```c
NetEndpoint *client = NULL;
NetEndpoint *server = NULL;
Net_CreateLocalPair(&client, &server);
```

---

## 2. Update and poll events

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

Normal loop: service the network, poll events, react based on event type.

---

## 3. Send typed messages

```c
NetPlayerMove move = {0};
GameplayInputCmd actions = {0};

Net_SendPlayerMove(endpoint, sequence++, tick, &move);
Net_SendInputCmd(endpoint, sequence++, tick, &actions);
```

Use `Net_SendPacket` only when you intentionally need to send raw pre-encoded data. Most game code should stick to the typed send helpers.

---

## 4. Shut down cleanly

```c
Net_Close(endpoint);
Net_Destroy(endpoint);
```

---

## Where this connects in the project

Main integration points:

- Client runtime: `src/game/game.c`
- Authoritative simulation: `src/server/server_core.c`
- Dedicated server entry point: `src/server/server_main.c`
- Diagnostics overlay: `src/diagnostics/net_profiler.c`

---

## Extra docs

- Movement sync overview: `docs/MOVEMENT_SYNC.md`
- Network diagnostics and profiler: `docs/NET_PROFILER.md`
