#ifndef RLVOXEL_DIAGNOSTICS_NET_PROFILER_H
#define RLVOXEL_DIAGNOSTICS_NET_PROFILER_H

#include <stddef.h>
#include <stdint.h>

#include "net/protocol.h"

/**
 * Lightweight in-game networking diagnostics panel.
 *
 * Tracks per-message packet/byte counters, recent packet log entries,
 * and input->server-confirm RTT estimates using `confirm_ref` bookkeeping.
 *
 * Lifecycle:
 * - Optional explicit `NetProfiler_Init()` on startup.
 * - Safe lazy-init on first record/draw call.
 * - `NetProfiler_Shutdown()` clears all state.
 */

/** Initializes profiler state and starts uptime timer. */
void NetProfiler_Init(void);
/** Clears all profiler state and marks profiler uninitialized. */
void NetProfiler_Shutdown(void);
/** Resets counters/history while preserving initialization state. */
void NetProfiler_Reset(void);

/**
 * Records one outgoing packet event.
 * `confirm_ref` is optional (0 means "not applicable").
 */
void NetProfiler_RecordSend(NetMessageType message_type, uint32_t sequence, uint32_t tick,
                            size_t packet_size, uint8_t channel, uint32_t confirm_ref);
/**
 * Records one incoming packet event.
 * `confirm_ref` is optional (0 means "not applicable").
 */
void NetProfiler_RecordReceive(NetMessageType message_type, uint32_t sequence, uint32_t tick,
                               size_t packet_size, uint8_t channel, uint32_t confirm_ref);

/** Draws the "Network Profiler" ImGui window. */
void NetProfiler_DrawWindow(void);

#endif
