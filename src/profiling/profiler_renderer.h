#ifndef PROFILER_RENDERER_H
#define PROFILER_RENDERER_H

void ProfilerRenderer_Init(void);
void ProfilerRenderer_Shutdown(void);

void ProfilerRenderer_DrawStatsTable(void);
void ProfilerRenderer_DrawFrameGraph(void);
void ProfilerRenderer_DrawConfigWindow(void);

#endif
