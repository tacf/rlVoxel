#include "diagnostics/net_profiler.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "net/protocol.h"
#include "raylib.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#define NET_PROFILER_MESSAGE_CAPACITY 32u
#define NET_PROFILER_EVENT_CAPACITY 256u
#define NET_PROFILER_INPUT_TRACK_CAPACITY 1024u
#define NET_PROFILER_EVENT_VIEW_ROWS 80u
#define NET_PROFILER_STATS_TABLE_HEIGHT 220.0f
#define NET_PROFILER_EVENTS_TABLE_HEIGHT 260.0f
#define NET_PROFILER_AGE_UPDATE_MS 250.0
#define NET_PROFILER_SCROLL_TOP_EPSILON 1.0f

typedef struct NetProfilerMessageStats {
  uint64_t tx_packets;
  uint64_t tx_bytes;
  uint64_t rx_packets;
  uint64_t rx_bytes;
  uint32_t last_tx_sequence;
  uint32_t last_tx_tick;
  uint8_t last_tx_channel;
  uint32_t last_rx_sequence;
  uint32_t last_rx_tick;
  uint8_t last_rx_channel;
  uint32_t last_confirm_ref;
} NetProfilerMessageStats;

typedef struct NetProfilerPacketEvent {
  double time_seconds;
  bool outgoing;
  NetMessageType message_type;
  uint32_t sequence;
  uint32_t tick;
  size_t packet_size;
  uint8_t channel;
  uint32_t confirm_ref;
} NetProfilerPacketEvent;

typedef struct NetProfilerInputTrack {
  uint32_t tick_id;
  double sent_time_seconds;
  bool valid;
} NetProfilerInputTrack;

typedef struct NetProfilerState {
  bool initialized;
  double start_time_seconds;

  uint64_t total_tx_packets;
  uint64_t total_rx_packets;
  uint64_t total_tx_bytes;
  uint64_t total_rx_bytes;

  uint64_t confirmed_inputs;
  uint32_t last_confirmed_input_tick;
  double last_confirm_rtt_ms;
  double avg_confirm_rtt_ms;
  double max_confirm_rtt_ms;

  NetProfilerMessageStats message_stats[NET_PROFILER_MESSAGE_CAPACITY];

  NetProfilerPacketEvent events[NET_PROFILER_EVENT_CAPACITY];
  size_t events_head;
  size_t events_count;
  bool packet_view_follow_top;
  size_t packet_view_head;
  size_t packet_view_count;

  NetProfilerInputTrack input_tracks[NET_PROFILER_INPUT_TRACK_CAPACITY];
} NetProfilerState;

static NetProfilerState g_net_profiler = {0};

static NetProfilerMessageStats *net_profiler_stats_for(NetMessageType message_type) {
  size_t index = (size_t)message_type;
  if (index >= NET_PROFILER_MESSAGE_CAPACITY) {
    return NULL;
  }
  return &g_net_profiler.message_stats[index];
}

static void net_profiler_push_event(bool outgoing, NetMessageType message_type, uint32_t sequence,
                                    uint32_t tick, size_t packet_size, uint8_t channel,
                                    uint32_t confirm_ref) {
  NetProfilerPacketEvent *event = &g_net_profiler.events[g_net_profiler.events_head];
  *event = (NetProfilerPacketEvent){
      .time_seconds = GetTime(),
      .outgoing = outgoing,
      .message_type = message_type,
      .sequence = sequence,
      .tick = tick,
      .packet_size = packet_size,
      .channel = channel,
      .confirm_ref = confirm_ref,
  };

  g_net_profiler.events_head = (g_net_profiler.events_head + 1u) % NET_PROFILER_EVENT_CAPACITY;
  if (g_net_profiler.events_count < NET_PROFILER_EVENT_CAPACITY) {
    g_net_profiler.events_count++;
  }
}

static void net_profiler_track_input_send(uint32_t input_tick_id, double now_seconds) {
  NetProfilerInputTrack *track;
  if (input_tick_id == 0u) {
    return;
  }

  track = &g_net_profiler.input_tracks[input_tick_id % NET_PROFILER_INPUT_TRACK_CAPACITY];
  *track = (NetProfilerInputTrack){
      .tick_id = input_tick_id,
      .sent_time_seconds = now_seconds,
      .valid = true,
  };
}

static void net_profiler_confirm_input(uint32_t confirmed_input_tick, double now_seconds) {
  NetProfilerInputTrack *track;
  double rtt_ms;

  if (confirmed_input_tick == 0u) {
    return;
  }

  track = &g_net_profiler.input_tracks[confirmed_input_tick % NET_PROFILER_INPUT_TRACK_CAPACITY];
  if (!track->valid || track->tick_id != confirmed_input_tick) {
    g_net_profiler.last_confirmed_input_tick = confirmed_input_tick;
    return;
  }

  rtt_ms = (now_seconds - track->sent_time_seconds) * 1000.0;
  if (rtt_ms < 0.0) {
    rtt_ms = 0.0;
  }

  g_net_profiler.last_confirmed_input_tick = confirmed_input_tick;
  g_net_profiler.last_confirm_rtt_ms = rtt_ms;
  if (g_net_profiler.confirmed_inputs == 0u) {
    g_net_profiler.avg_confirm_rtt_ms = rtt_ms;
    g_net_profiler.max_confirm_rtt_ms = rtt_ms;
  } else {
    double count = (double)g_net_profiler.confirmed_inputs + 1.0;
    g_net_profiler.avg_confirm_rtt_ms =
        g_net_profiler.avg_confirm_rtt_ms + (rtt_ms - g_net_profiler.avg_confirm_rtt_ms) / count;
    if (rtt_ms > g_net_profiler.max_confirm_rtt_ms) {
      g_net_profiler.max_confirm_rtt_ms = rtt_ms;
    }
  }
  g_net_profiler.confirmed_inputs++;
  track->valid = false;
}

void NetProfiler_Init(void) {
  memset(&g_net_profiler, 0, sizeof(g_net_profiler));
  g_net_profiler.initialized = true;
  g_net_profiler.start_time_seconds = GetTime();
  g_net_profiler.packet_view_follow_top = true;
}

void NetProfiler_Shutdown(void) { memset(&g_net_profiler, 0, sizeof(g_net_profiler)); }

void NetProfiler_Reset(void) {
  bool was_initialized = g_net_profiler.initialized;
  NetProfiler_Init();
  g_net_profiler.initialized = was_initialized;
}

void NetProfiler_RecordSend(NetMessageType message_type, uint32_t sequence, uint32_t tick,
                            size_t packet_size, uint8_t channel, uint32_t confirm_ref) {
  NetProfilerMessageStats *stats;
  double now_seconds;

  if (!g_net_profiler.initialized) {
    NetProfiler_Init();
  }

  now_seconds = GetTime();
  stats = net_profiler_stats_for(message_type);
  if (stats != NULL) {
    stats->tx_packets++;
    stats->tx_bytes += (uint64_t)packet_size;
    stats->last_tx_sequence = sequence;
    stats->last_tx_tick = tick;
    stats->last_tx_channel = channel;
    if (confirm_ref != 0u) {
      stats->last_confirm_ref = confirm_ref;
    }
  }

  g_net_profiler.total_tx_packets++;
  g_net_profiler.total_tx_bytes += (uint64_t)packet_size;
  net_profiler_push_event(true, message_type, sequence, tick, packet_size, channel, confirm_ref);

  if (message_type == NET_MSG_C2S_INPUT_CMD || message_type == NET_MSG_C2S_PLAYER_MOVE) {
    uint32_t input_tick = (confirm_ref != 0u) ? confirm_ref : tick;
    net_profiler_track_input_send(input_tick, now_seconds);
  }
}

void NetProfiler_RecordReceive(NetMessageType message_type, uint32_t sequence, uint32_t tick,
                               size_t packet_size, uint8_t channel, uint32_t confirm_ref) {
  NetProfilerMessageStats *stats;
  double now_seconds;

  if (!g_net_profiler.initialized) {
    NetProfiler_Init();
  }

  now_seconds = GetTime();
  stats = net_profiler_stats_for(message_type);
  if (stats != NULL) {
    stats->rx_packets++;
    stats->rx_bytes += (uint64_t)packet_size;
    stats->last_rx_sequence = sequence;
    stats->last_rx_tick = tick;
    stats->last_rx_channel = channel;
    if (confirm_ref != 0u) {
      stats->last_confirm_ref = confirm_ref;
    }
  }

  g_net_profiler.total_rx_packets++;
  g_net_profiler.total_rx_bytes += (uint64_t)packet_size;
  net_profiler_push_event(false, message_type, sequence, tick, packet_size, channel, confirm_ref);

  if (message_type == NET_MSG_S2C_PLAYER_STATE && confirm_ref != 0u) {
    net_profiler_confirm_input(confirm_ref, now_seconds);
  }
}

void NetProfiler_DrawWindow(void) {
  double now_seconds;
  double elapsed_seconds;

  if (!igBegin("Network Profiler", NULL, 0)) {
    igEnd();
    return;
  }

  if (!g_net_profiler.initialized) {
    NetProfiler_Init();
  }

  if (igButton("Reset Counters", (ImVec2){0.0f, 0.0f})) {
    NetProfiler_Reset();
  }

  now_seconds = GetTime();
  elapsed_seconds = now_seconds - g_net_profiler.start_time_seconds;
  if (elapsed_seconds <= 0.0001) {
    elapsed_seconds = 0.0001;
  }

  igSeparatorText("Overview");
  igText("Uptime: %.2f s", elapsed_seconds);
  igText("TX Total Bytes: %llu", (unsigned long long)g_net_profiler.total_tx_bytes);
  igText("RX Total Bytes: %llu", (unsigned long long)g_net_profiler.total_rx_bytes);
  igText("TX: %llu pkts | %.2f KB | %.2f pkt/s",
         (unsigned long long)g_net_profiler.total_tx_packets,
         (double)g_net_profiler.total_tx_bytes / 1024.0,
         (double)g_net_profiler.total_tx_packets / elapsed_seconds);
  igText("RX: %llu pkts | %.2f KB | %.2f pkt/s",
         (unsigned long long)g_net_profiler.total_rx_packets,
         (double)g_net_profiler.total_rx_bytes / 1024.0,
         (double)g_net_profiler.total_rx_packets / elapsed_seconds);

  igSeparatorText("Input Confirmation");
  if (g_net_profiler.confirmed_inputs == 0u) {
    igText("No confirmed input ticks yet.");
    if (g_net_profiler.last_confirmed_input_tick != 0u) {
      igText("Latest ack ref (no local send sample): %u", g_net_profiler.last_confirmed_input_tick);
    }
  } else {
    igText("Confirmed Inputs: %llu", (unsigned long long)g_net_profiler.confirmed_inputs);
    igText("Last Ack Tick: %u", g_net_profiler.last_confirmed_input_tick);
    igText("Last RTT: %.2f ms", g_net_profiler.last_confirm_rtt_ms);
    igText("Avg RTT: %.2f ms", g_net_profiler.avg_confirm_rtt_ms);
    igText("Max RTT: %.2f ms", g_net_profiler.max_confirm_rtt_ms);
  }

  igSeparatorText("Per Message Type");
  if (igBeginTable("NetMessageStats", 10,
                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
                   (ImVec2){0.0f, NET_PROFILER_STATS_TABLE_HEIGHT}, 0.0f)) {
    igTableSetupColumn("Message", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("TX Pkts", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("TX KB", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("RX Pkts", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("RX KB", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("Last TX Seq", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("Last RX Seq", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("Last RX Tick", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("Ch (TX/RX)", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("Confirm Ref", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableHeadersRow();

    for (size_t i = 0; i < NET_PROFILER_MESSAGE_CAPACITY; i++) {
      const NetProfilerMessageStats *stats = &g_net_profiler.message_stats[i];
      if (stats->tx_packets == 0u && stats->rx_packets == 0u) {
        continue;
      }

      igTableNextRow(0, 0.0f);
      igTableNextColumn();
      igText("%s", Protocol_MessageTypeName((NetMessageType)i));
      igTableNextColumn();
      igText("%llu", (unsigned long long)stats->tx_packets);
      igTableNextColumn();
      igText("%.2f", (double)stats->tx_bytes / 1024.0);
      igTableNextColumn();
      igText("%llu", (unsigned long long)stats->rx_packets);
      igTableNextColumn();
      igText("%.2f", (double)stats->rx_bytes / 1024.0);
      igTableNextColumn();
      igText("%u", stats->last_tx_sequence);
      igTableNextColumn();
      igText("%u", stats->last_rx_sequence);
      igTableNextColumn();
      igText("%u", stats->last_rx_tick);
      igTableNextColumn();
      igText("%u/%u", (unsigned int)stats->last_tx_channel, (unsigned int)stats->last_rx_channel);
      igTableNextColumn();
      igText("%u", stats->last_confirm_ref);
    }

    igEndTable();
  }

  igSeparatorText("Recent Packets");
  size_t view_head = g_net_profiler.events_head;
  size_t view_count = g_net_profiler.events_count;
  if (!g_net_profiler.packet_view_follow_top) {
    view_head = g_net_profiler.packet_view_head;
    view_count = g_net_profiler.packet_view_count;
  }

  if (igBeginTable("NetPacketEvents", 8,
                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                       ImGuiTableFlags_ScrollY,
                   (ImVec2){0.0f, NET_PROFILER_EVENTS_TABLE_HEIGHT}, 0.0f)) {
    igTableSetupScrollFreeze(0, 1);
    igTableSetupColumn("Time", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("Dir", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("Message", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("Seq/Tick", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("Channel", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("Bytes", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("Confirm Ref", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableSetupColumn("Age (ms)", ImGuiTableColumnFlags_None, 0.0f, 0);
    igTableHeadersRow();

    for (size_t row = 0; row < view_count && row < NET_PROFILER_EVENT_VIEW_ROWS; row++) {
      size_t idx =
          (view_head + NET_PROFILER_EVENT_CAPACITY - 1u - row) % NET_PROFILER_EVENT_CAPACITY;
      const NetProfilerPacketEvent *event = &g_net_profiler.events[idx];
      double rel_time = event->time_seconds - g_net_profiler.start_time_seconds;
      double age_ms = (now_seconds - event->time_seconds) * 1000.0;
      uint64_t age_bucket = (uint64_t)(age_ms / NET_PROFILER_AGE_UPDATE_MS);
      double age_display_ms = (double)age_bucket * NET_PROFILER_AGE_UPDATE_MS;

      igTableNextRow(0, 0.0f);
      igTableNextColumn();
      igText("%.3f s", rel_time);
      igTableNextColumn();
      igText("%s", event->outgoing ? "TX" : "RX");
      igTableNextColumn();
      igText("%s", Protocol_MessageTypeName(event->message_type));
      igTableNextColumn();
      igText("%u / %u", event->sequence, event->tick);
      igTableNextColumn();
      igText("%u", (unsigned int)event->channel);
      igTableNextColumn();
      igText("%u", (unsigned int)event->packet_size);
      igTableNextColumn();
      igText("%u", event->confirm_ref);
      igTableNextColumn();
      igText("%.0f", age_display_ms);
    }

    {
      float scroll_y = igGetScrollY();
      bool at_top = scroll_y <= NET_PROFILER_SCROLL_TOP_EPSILON;
      if (g_net_profiler.packet_view_follow_top) {
        if (!at_top) {
          g_net_profiler.packet_view_follow_top = false;
          g_net_profiler.packet_view_head = g_net_profiler.events_head;
          g_net_profiler.packet_view_count = g_net_profiler.events_count;
        }
      } else if (at_top) {
        g_net_profiler.packet_view_follow_top = true;
      }
    }

    igEndTable();
  }

  igEnd();
}
