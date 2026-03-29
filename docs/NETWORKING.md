# Networking Module

## What this module does

`libnet` gives the game one networking system that works in both runtime modes:

- **Internal singleplayer**: the client and the server both run inside the same process and talk through a local in-memory connection.
- **Dedicated multiplayer**: the client and server talk over the network using ENet and UDP.

The main goal is to keep the game **server-authoritative** in both cases. That means the server is always the final source of truth, even in singleplayer.

---

## Big picture

The networking code is split into three layers:

### 1. `transport` (`libnet/include/net/transport.h`)
This is the lowest layer.

It only handles moving packets from one place to another.

Backends:
- ENet
- local in-memory transport pair

### 2. `protocol` (`libnet/include/net/protocol.h`)
This layer defines the wire format.

It handles:
- message types
- versioning
- packet encoding and decoding
- shared packet metadata helpers

Useful helpers:
- `Protocol_MessageTypeName(type)`
- `Protocol_FixedPayloadSize(type)` — returns `0` for variable-size payloads
- `Protocol_FixedPacketSize(type)` — returns `0` for variable-size packets

### 3. `net` facade (`libnet/include/net/net.h`)
This is the gameplay-facing API.

Game and server code use this layer directly through functions like:
- `Net_Update`
- `Net_PollEvent`
- `Net_Send*`

It also decides which channel and reliability mode each message type uses.

---

## Build target

`libnet` builds as the static target `net` and is linked into `rlvoxel_runtime`.

---

## Runtime modes

## Internal singleplayer

In singleplayer, the game still uses the same client/server flow.
The only difference is that both sides are running in the same process.

How it works:

1. Create a local endpoint pair with `Net_CreateLocalPair`.
2. Give the server endpoint to `ServerCore`.
3. Keep the client endpoint in `Game`.
4. Run the same protocol and events used by dedicated multiplayer.

So even offline singleplayer still goes through the network layer.
That keeps behavior consistent.

## Dedicated server

In multiplayer:

1. The server starts listening with `Net_Listen`.
2. The client connects with `Net_Connect`.
3. The same typed protocol is used, but packets are carried by ENet.

---

## Protocol v2

Every packet starts with a `NetMessageHeader`.

It contains:
- `magic` (`RVNET_MAGIC`)
- `version` (`RVNET_VERSION`)
- `type`
- `sequence`
- `tick`

### Message types

The current message set is:

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

The main exception is `S2C_Disconnect`, because it includes a text reason and can vary in size.
For that message, the fixed-size helpers return `0`.

---

## ENet channel rules

### Channel 0 — reliable and ordered
Used for important messages that must arrive and stay in order:

- hello / welcome
- chunk data
- block updates
- chunk unload
- disconnect

### Channel 1 — gameplay updates
Used for movement and player state.

- `C2S_PlayerMove`: **unreliable sequenced**  
  For high-rate movement updates. Newer data replaces older data.

- `C2S_InputCmd`: **reliable ordered**  
  For action presses/releases, selected block, and authority-related gameplay input.

  This message also carries:
  - gameplay mode (`creative` or `survival`)
  - fly toggle state

- `S2C_PlayerState`: **reliable ordered**  
  Used for corrections and periodic synchronization from the server.

---

## `S2C_PlayerState` timeline fields

`S2C_PlayerState` includes two important timeline anchors:

- `tick_id`: the current server simulation tick
- `input_tick_id`: the latest `C2S_PlayerMove.tick_id` that the server accepted

These values are important for client prediction and reconciliation.

---

## Movement sync (v2)

## Basic idea

Movement runs on a fixed **20 ticks per second** simulation (`GAME_TICK_RATE`).

The client predicts its own movement locally so controls feel responsive.
At the same time, the server stays authoritative and decides what is actually valid.

### The client sends
- `C2S_PlayerMove`: player position, velocity, look direction, and related movement state
- `C2S_InputCmd`: input actions, selected block, gameplay mode, and fly state

### The server sends
- `S2C_PlayerState`: corrections and regular sync updates

---

## Client-side flow

Source: `src/game/game.c`

### During each frame
The client:
- reads player input
- applies look changes immediately
- merges input into the pending tick state

There is **no camera-look smoothing** here.

### During each tick
The client:
1. processes incoming network events first
2. predicts local player movement
3. sends `C2S_PlayerMove`
4. sends `C2S_InputCmd` when something meaningful changes, plus occasional keepalive updates
5. stores a prediction sample using the current tick id

### When `S2C_PlayerState` arrives
The client:
1. uses `input_tick_id` to find the matching point in its prediction history
2. checks whether the difference is tiny or meaningful
3. if the difference is tiny, it just treats the state as acknowledged
4. if the difference is bigger, it resets to the authoritative server state and replays any unacknowledged predicted inputs

In simple terms: the client predicts first, then fixes itself when the server disagrees.

---

## Server-side flow

Source: `src/server/server_core.c`

### During each tick
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

## Why `input_tick_id` is important

This field helps the client compare its prediction against the **same input timeline** the server actually processed.

That matters because reconciliation only works properly when both sides are talking about the same moment in the input history.

Without `input_tick_id`, the client can compare against the wrong prediction sample, which often causes repeated rubberbanding or bad corrections.

---

## Debug checklist

If movement sync feels wrong, check these first:

- The client and server are built with the same protocol version.
- `S2C_PlayerState.input_tick_id` is increasing as expected.
- The client still has matching prediction history for the acknowledged ticks.
- After a correction, the client replays unacknowledged inputs.
- Big corrections are rare and only happen when there is real divergence.

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

This is the normal loop:
- service the network
- poll events
- react based on event type

---

## 3. Send typed messages

```c
NetPlayerMove move = {0};
GameplayInputCmd actions = {0};

Net_SendPlayerMove(endpoint, sequence++, tick, &move);
Net_SendInputCmd(endpoint, sequence++, tick, &actions);
```

Use `Net_SendPacket` only when you intentionally need to send raw pre-encoded data.
Most game code should stick to the typed send helpers.

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
