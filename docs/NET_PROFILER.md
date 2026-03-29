# Network Profiler

## Purpose

The in-game Network Profiler provides a quick runtime view of packet flow without external tooling.

## Data Tracked

### Overview Counters

- Total TX packets/bytes.
- Total RX packets/bytes.
- Rate estimates (`pkt/s`) based on profiler uptime.

### Per-Message-Type Table

For each `NetMessageType` seen:

- TX packet count + TX KB.
- RX packet count + RX KB.
- Last TX/RX sequence and last RX tick.
- Last channel seen.
- Last `confirm_ref` seen.

### Input Confirmation Metrics

Uses local send timestamps keyed by input tick id and measures confirmation delay when
`S2C_PlayerState.confirm_ref`/`input_tick_id` advances.

Displayed fields:

- Confirmed input count.
- Last confirmed input tick.
- Last/Avg/Max confirmation RTT (ms).

## Recent Packets View

The packet log table listing all packet exchanges as well as sizes and ack references to reliable packages.

## Reset and Lifecycle

- `NetProfiler_Init()`: initialize and start uptime baseline.
- `NetProfiler_Reset()`: clear counters/history while preserving initialized state.
- `NetProfiler_Shutdown()`: clear all state.

Lazy init is supported; calling record/draw before explicit init is safe.

## Notes

The information displayed is related to the network protocol so refer to `docs/NETWORKING.md` for details.
