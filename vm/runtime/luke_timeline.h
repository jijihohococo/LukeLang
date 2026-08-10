#ifndef LUKE_TIMELINE_H
#define LUKE_TIMELINE_H

/* Phase 6 — reactive timeline progress cells (0→1 lerp into target cell). */

#include "luke_reactive.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LukeRxTimelineJob {
  char id[64];
  LukeRxId target;
  double from;
  double to;
  int active;
} LukeRxTimelineJob;

static inline void luke_rx_timeline_id_copy(char *dst, size_t cap, LukeText id) {
  size_t n = id.len < cap - 1 ? id.len : cap - 1;
  if (n && id.ptr) memcpy(dst, id.ptr, n);
  dst[n] = '\0';
}

static inline LukeRxTimelineJob *luke_rx_timeline_find(LukeRxGraph *g, LukeText id) {
  if (!g) return NULL;
  for (size_t i = 0; i < g->timeline_len; ++i) {
    if (strcmp(g->timelines[i].id, "") == 0) continue;
    char tmp[64];
    luke_rx_timeline_id_copy(tmp, sizeof(tmp), id);
    if (strcmp(g->timelines[i].id, tmp) == 0) return (LukeRxTimelineJob *)&g->timelines[i];
  }
  return NULL;
}

static inline void luke_rx_timeline_register(LukeRxGraph *g, LukeText id, LukeRxId target,
                                             double from, double to) {
  if (!g || !target) return;
  LukeRxTimelineJob *j = luke_rx_timeline_find(g, id);
  if (!j) {
    if (g->timeline_len >= LUKE_RX_MAX_TIMELINES) return;
    j = (LukeRxTimelineJob *)&g->timelines[g->timeline_len++];
    memset(j, 0, sizeof(*j));
    luke_rx_timeline_id_copy(j->id, sizeof(j->id), id);
  }
  j->target = target;
  j->from = from;
  j->to = to;
  j->active = 1;
}

static inline void luke_rx_timeline_progress(LukeRxGraph *g, LukeText id, double t01) {
  LukeRxTimelineJob *j = luke_rx_timeline_find(g, id);
  if (!j || !j->active) return;
  if (t01 < 0) t01 = 0;
  if (t01 > 1) t01 = 1;
  double v = j->from + (j->to - j->from) * t01;
  luke_rx_write_num(g, j->target, v);
}

static inline void luke_rx_timeline_finish(LukeRxGraph *g, LukeText id) {
  LukeRxTimelineJob *j = luke_rx_timeline_find(g, id);
  if (!j) return;
  luke_rx_write_num(g, j->target, j->to);
  j->active = 0;
}

static inline void luke_rx_timeline_run_sync(LukeRxGraph *g, LukeText id, double from, double to,
                                             LukeRxId target, int steps) {
  if (!g || !target || steps < 1) steps = 1;
  luke_rx_timeline_register(g, id, target, from, to);
  luke_rx_batch_begin(g);
  for (int i = 0; i <= steps; ++i) {
    double t = (double)i / (double)steps;
    luke_rx_timeline_progress(g, id, t);
  }
  luke_rx_batch_end(g);
  luke_rx_timeline_finish(g, id);
}

#ifdef __cplusplus
}
#endif

#endif /* LUKE_TIMELINE_H */
