#include "profiler_renderer.h"
#include "profiler.h"

#include <string.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#ifndef IM_COL32
#define IM_COL32_R_SHIFT 0
#define IM_COL32_G_SHIFT 8
#define IM_COL32_B_SHIFT 16
#define IM_COL32_A_SHIFT 24
#define IM_COL32(R, G, B, A)                                                                       \
  (((ImU32)(A) << IM_COL32_A_SHIFT) | ((ImU32)(B) << IM_COL32_B_SHIFT) |                           \
   ((ImU32)(G) << IM_COL32_G_SHIFT) | ((ImU32)(R) << IM_COL32_R_SHIFT))
#endif

typedef struct SectionAggregate {
  double history[PROFILER_HISTORY_SIZE];
  int history_index;
  int sample_count;
  double last_time_ms;
  double avg_time_ms;
  double max_time_ms;
  double hit_rate_percent;
} SectionAggregate;

static struct {
  int selected_section_index;
  bool initialized;
} g_renderer_state = {0};

static void RenderSectionRow(const ProfilerSection *section, int index, const Profiler *profiler);
static bool HasChildren(int section_index, const Profiler *profiler);
static void RenderSectionTreeNode(const ProfilerSection *section, int index,
                                  const Profiler *profiler);
static void RenderFrameGraph(int section_index, const Profiler *profiler);
static void DrawReferenceLine(ImDrawList *draw_list, ImVec2 canvas_pos, ImVec2 canvas_size,
                              float time_ms, float max_ms, unsigned int color, const char *label);
static void BuildSectionAggregate(int section_index, const Profiler *profiler,
                                  SectionAggregate *aggregate);
static void AccumulateSectionHistoryRecursive(int section_index, const Profiler *profiler,
                                              double *history, unsigned int *max_sampled_frames);

void ProfilerRenderer_Init(void) {
  g_renderer_state.selected_section_index = -1;
  g_renderer_state.initialized = true;
}

void ProfilerRenderer_Shutdown(void) {
  g_renderer_state.selected_section_index = -1;
  g_renderer_state.initialized = false;
}

void ProfilerRenderer_DrawStatsTable(void) {
  if (!igBegin("Profiler Statistics", NULL, 0)) {
    igEnd();
    return;
  }

  const Profiler *profiler = Profiler_Get();

  if (igBeginTable("ProfilerTable", 5,
                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
                   (ImVec2){0, 0}, 0)) {

    igTableSetupColumn("Section", ImGuiTableColumnFlags_None, 0, 0);
    igTableSetupColumn("Current (ms)", ImGuiTableColumnFlags_None, 0, 0);
    igTableSetupColumn("Frame Avg (ms)", ImGuiTableColumnFlags_None, 0, 0);
    igTableSetupColumn("Window Max (ms)", ImGuiTableColumnFlags_None, 0, 0);
    igTableSetupColumn("Hit Rate", ImGuiTableColumnFlags_None, 0, 0);
    igTableHeadersRow();

    for (int i = 0; i < profiler->section_count; i++) {
      if (profiler->sections[i].parent_index == -1) {
        RenderSectionRow(&profiler->sections[i], i, profiler);
      }
    }

    igEndTable();
  }

  igEnd();
}

void ProfilerRenderer_DrawFrameGraph(void) {
  if (!igBegin("Frame Time Graph", NULL, 0)) {
    igEnd();
    return;
  }

  const Profiler *profiler = Profiler_Get();

  igColumns(2, "GraphColumns", true);
  igSetColumnWidth(0, 220.0f);

  igBeginChild_Str("SectionTree", (ImVec2){0, 0}, true, 0);
  for (int i = 0; i < profiler->section_count; i++) {
    if (profiler->sections[i].parent_index == -1) {
      RenderSectionTreeNode(&profiler->sections[i], i, profiler);
    }
  }
  igEndChild();

  igNextColumn();

  igBeginChild_Str("Graph", (ImVec2){0, 0}, true, 0);
  if ((g_renderer_state.selected_section_index >= 0) &&
      (g_renderer_state.selected_section_index < profiler->section_count)) {
    RenderFrameGraph(g_renderer_state.selected_section_index, profiler);
  } else {
    igText("Select a section to view graph data.");
  }
  igEndChild();

  igColumns(1, NULL, false);
  igEnd();
}

void ProfilerRenderer_DrawConfigWindow(void) {}

static bool HasChildren(int section_index, const Profiler *profiler) {
  for (int i = 0; i < profiler->section_count; i++) {
    if (profiler->sections[i].parent_index == section_index) {
      return true;
    }
  }
  return false;
}

static void RenderSectionRow(const ProfilerSection *section, int index, const Profiler *profiler) {
  SectionAggregate aggregate;
  BuildSectionAggregate(index, profiler, &aggregate);

  igTableNextRow(0, 0);
  igTableNextColumn();

  for (int i = 0; i < section->depth; i++) {
    igIndent(20.0f);
  }

  bool has_children = HasChildren(index, profiler);
  bool node_open = false;

  if (has_children) {
    node_open = igTreeNodeEx_Str(section->name, ImGuiTreeNodeFlags_SpanFullWidth);
  } else {
    igText("%s", section->name);
  }

  igTableNextColumn();
  igText("%.3f", aggregate.last_time_ms);

  igTableNextColumn();
  igText("%.3f", aggregate.avg_time_ms);

  igTableNextColumn();
  igText("%.3f", aggregate.max_time_ms);

  igTableNextColumn();
  igText("%.1f%%", aggregate.hit_rate_percent);

  if (has_children && node_open) {
    for (int i = 0; i < profiler->section_count; i++) {
      if (profiler->sections[i].parent_index == index) {
        RenderSectionRow(&profiler->sections[i], i, profiler);
      }
    }
    igTreePop();
  }

  for (int i = 0; i < section->depth; i++) {
    igUnindent(20.0f);
  }
}

static void RenderSectionTreeNode(const ProfilerSection *section, int index,
                                  const Profiler *profiler) {
  bool has_children = HasChildren(index, profiler);
  bool node_open = false;
  bool is_selected = (g_renderer_state.selected_section_index == index);

  if (has_children) {
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (is_selected) {
      flags |= ImGuiTreeNodeFlags_Selected;
    }
    node_open = igTreeNodeEx_Str(section->name, flags);

    if (igIsItemClicked(0)) {
      g_renderer_state.selected_section_index = index;
    }
  } else {
    if (igSelectable_Bool(section->name, is_selected, 0, (ImVec2){0, 0})) {
      g_renderer_state.selected_section_index = index;
    }
  }

  if (has_children && node_open) {
    for (int i = 0; i < profiler->section_count; i++) {
      if (profiler->sections[i].parent_index == index) {
        RenderSectionTreeNode(&profiler->sections[i], i, profiler);
      }
    }
    igTreePop();
  }
}

static void RenderFrameGraph(int section_index, const Profiler *profiler) {
  SectionAggregate aggregate;
  BuildSectionAggregate(section_index, profiler, &aggregate);

  const char *section_name = profiler->sections[section_index].name;
  igText("Section: %s", section_name);
  igText("Current: %.3f ms | Avg: %.3f ms | Max: %.3f ms | Hit: %.1f%%", aggregate.last_time_ms,
         aggregate.avg_time_ms, aggregate.max_time_ms, aggregate.hit_rate_percent);
  igSpacing();

  ImVec2 canvas_pos, canvas_size;
  igGetCursorScreenPos(&canvas_pos);
  igGetContentRegionAvail(&canvas_size);

  if (canvas_size.y < 120.0f) {
    canvas_size.y = 120.0f;
  }

  ImDrawList *draw_list = igGetWindowDrawList();
  ImDrawList_AddRectFilled(draw_list, canvas_pos,
                           (ImVec2){canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y},
                           IM_COL32(20, 20, 20, 255), 0, 0);

  float max_ms = 50.0f;
  DrawReferenceLine(draw_list, canvas_pos, canvas_size, 16.6f, max_ms, IM_COL32(0, 255, 0, 255),
                    "60 FPS");
  DrawReferenceLine(draw_list, canvas_pos, canvas_size, 33.3f, max_ms, IM_COL32(255, 255, 0, 255),
                    "30 FPS");
  DrawReferenceLine(draw_list, canvas_pos, canvas_size, 6.9f, max_ms, IM_COL32(0, 255, 255, 255),
                    "144 FPS");

  float bar_width = canvas_size.x / PROFILER_HISTORY_SIZE;
  for (int i = 0; i < PROFILER_HISTORY_SIZE; i++) {
    int history_idx = (aggregate.history_index + i) % PROFILER_HISTORY_SIZE;
    double value = aggregate.history[history_idx];

    if (value > 0.0) {
      float bar_height = (float)(value / max_ms) * canvas_size.y;
      if (bar_height > canvas_size.y) {
        bar_height = canvas_size.y;
      }
      float x = canvas_pos.x + i * bar_width;
      float y = canvas_pos.y + canvas_size.y - bar_height;

      ImDrawList_AddRectFilled(draw_list, (ImVec2){x, y},
                               (ImVec2){x + bar_width, canvas_pos.y + canvas_size.y},
                               IM_COL32(100, 150, 255, 255), 0, 0);
    }
  }

  igDummy(canvas_size);
}

static void DrawReferenceLine(ImDrawList *draw_list, ImVec2 canvas_pos, ImVec2 canvas_size,
                              float time_ms, float max_ms, unsigned int color, const char *label) {
  float y = canvas_pos.y + canvas_size.y - (time_ms / max_ms) * canvas_size.y;

  ImDrawList_AddLine(draw_list, (ImVec2){canvas_pos.x, y},
                     (ImVec2){canvas_pos.x + canvas_size.x, y}, color, 1.0f);
  ImDrawList_AddText_Vec2(draw_list, (ImVec2){canvas_pos.x + 5.0f, y - 15.0f}, color, label, NULL);
}

static void BuildSectionAggregate(int section_index, const Profiler *profiler,
                                  SectionAggregate *aggregate) {
  memset(aggregate, 0, sizeof(*aggregate));
  aggregate->history_index = profiler->sections[section_index].history_index;

  unsigned int max_sampled_frames = 0;
  AccumulateSectionHistoryRecursive(section_index, profiler, aggregate->history,
                                    &max_sampled_frames);

  int sample_count = (int)max_sampled_frames;
  if (sample_count > PROFILER_HISTORY_SIZE) {
    sample_count = PROFILER_HISTORY_SIZE;
  }
  aggregate->sample_count = sample_count;

  if (sample_count <= 0) {
    return;
  }

  int start_index =
      (aggregate->history_index - sample_count + PROFILER_HISTORY_SIZE) % PROFILER_HISTORY_SIZE;

  double total_ms = 0.0;
  double max_ms = 0.0;
  int hit_count = 0;

  for (int i = 0; i < sample_count; i++) {
    int history_index = (start_index + i) % PROFILER_HISTORY_SIZE;
    double value = aggregate->history[history_index];

    total_ms += value;
    if (value > max_ms) {
      max_ms = value;
    }
    if (value > 0.0) {
      hit_count++;
    }
  }

  int latest_index = (aggregate->history_index + PROFILER_HISTORY_SIZE - 1) % PROFILER_HISTORY_SIZE;
  aggregate->last_time_ms = aggregate->history[latest_index];
  aggregate->avg_time_ms = total_ms / (double)sample_count;
  aggregate->max_time_ms = max_ms;
  aggregate->hit_rate_percent = (double)hit_count * 100.0 / (double)sample_count;
}

static void AccumulateSectionHistoryRecursive(int section_index, const Profiler *profiler,
                                              double *history, unsigned int *max_sampled_frames) {
  const ProfilerSection *section = &profiler->sections[section_index];

  if (section->total_sampled_frames > *max_sampled_frames) {
    *max_sampled_frames = section->total_sampled_frames;
  }

  for (int i = 0; i < PROFILER_HISTORY_SIZE; i++) {
    history[i] += section->history[i];
  }

  for (int i = 0; i < profiler->section_count; i++) {
    if (profiler->sections[i].parent_index == section_index) {
      AccumulateSectionHistoryRecursive(i, profiler, history, max_sampled_frames);
    }
  }
}
