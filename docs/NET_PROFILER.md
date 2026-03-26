# Network Profiler

## Purpose

The in-game Network Profiler provides a quick runtime view of packet flow without external tooling.

It helps answer:

- Are we sending or receiving too many packets?
- Which message types dominate traffic?
- Are authoritative movement confirmations arriving and how fast?
- Are packet streams advancing by sequence/tick as expected?

## Where It Lives

- API: `src/diagnostics/net_profiler.h`
- Implementation: `src/diagnostics/net_profiler.c`
- Call sites: client/server networking paths in `src/game/` and `src/server/`

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

The packet log table is a bounded live buffer with UX behavior similar to common live logs:

- Header row is frozen (static) while scrolling.
- If scroll is at the top, view follows live events.
- If user scrolls away from top, view is pinned and new packets do not push rows.
- Returning to top resumes live follow mode.

Age display is intentionally quantized (coarse updates) to improve readability under high event rates.

## Reset and Lifecycle

- `NetProfiler_Init()`: initialize and start uptime baseline.
- `NetProfiler_Reset()`: clear counters/history while preserving initialized state.
- `NetProfiler_Shutdown()`: clear all state.

Lazy init is supported; calling record/draw before explicit init is safe.

## Usage Notes

- Profiler is diagnostics-only and should not change network behavior.
- `confirm_ref = 0` is treated as "not applicable".
- Message labels and fixed packet-size metadata come from `libnet` protocol helpers
  (single source of truth), so profiler UI/logs stay in sync with protocol enum changes.
- Use in conjunction with `docs/NETWORKING.md` and `docs/MOVEMENT_SYNC.md` for protocol semantics.
