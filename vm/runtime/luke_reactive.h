#ifndef LUKE_REACTIVE_H
#define LUKE_REACTIVE_H

/* Luke Reactive Runtime — Phases 1–3
 * See docs/REACTIVE.md
 *
 * Doctrine: Lukelang understands change.
 * Cells / derived / effects + batched scheduler + component scopes.
 * Argus/Hanka consume invalidation; they are not the reactive framework.
 *
 * Arena-backed graph. No GC. Single-threaded.
 */

#include "luke_rt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t LukeRxId;

typedef enum LukeRxKind {
  LUKE_RX_CELL = 1,
  LUKE_RX_DERIVED = 2,
  LUKE_RX_EFFECT = 3,
  LUKE_RX_LIST = 4,
  LUKE_RX_MAP = 5
} LukeRxKind;

typedef enum LukeRxWave {
  LUKE_RX_WAVE_INVALIDATE = 1,
  LUKE_RX_WAVE_RECOMPUTE = 2,
  LUKE_RX_WAVE_EFFECT = 3,
  LUKE_RX_WAVE_LAYOUT = 4,
  LUKE_RX_WAVE_PAINT = 5
} LukeRxWave;

typedef enum LukeRxPriority {
  LUKE_RX_PRIO_UI = 0,
  LUKE_RX_PRIO_NORMAL = 1,
  LUKE_RX_PRIO_BACKGROUND = 2
} LukeRxPriority;

typedef struct LukeRxGraph LukeRxGraph;

typedef double (*LukeRxComputeFn)(LukeRxGraph *g, void *ctx);
typedef void (*LukeRxEffectFn)(LukeRxGraph *g, void *ctx);
typedef void (*LukeRxAfterFlushFn)(LukeRxGraph *g);

typedef enum LukeRxValKind {
  LUKE_RX_VAL_NUM = 0,
  LUKE_RX_VAL_TEXT = 1,
  LUKE_RX_VAL_LIST = 2,
  LUKE_RX_VAL_MAP = 3,
  LUKE_RX_VAL_INT = 4
} LukeRxValKind;

typedef struct LukeRxNode {
  LukeRxId id;
  LukeRxKind kind;
  LukeRxValKind vkind;
  int dirty;
  int dead; /* disposed with component scope */
  int weak; /* effect: reads do not register dependency edges */
  int errored; /* isolated failure — skipped until retry */
  int queued; /* enqueued on dirty_q this flush turn */
  uint8_t priority; /* effect scheduling lane (Scheduler 2.0) */
  uint8_t wait_epochs; /* starvation guard — epochs waiting to run */
  int version;
  uint32_t scope_id; /* owning scope frame (0 = global) */
  uint32_t boundary_scope_id; /* error boundary scope (0 = none) */
  char name[48]; /* debugger / DAP label (empty if unnamed) */
  double num;
  int64_t i64; /* exact INTEGER cells (IDs / money cents / counters) */
  LukeText text;
  LukeList *list;
  LukeMap *map;
  int change_kind; /* 0 none, 1 structural (add/len), 2 item/slot */
  int last_index;  /* index (or map slot) touched when change_kind==2; else new len-1 */
  LukeRxComputeFn compute;
  LukeRxEffectFn effect;
  void *ctx;
  /* Adjacency (arena-grown; dep = what I read; sub = who reads me) */
  LukeRxId *deps;
  size_t dep_len;
  size_t dep_cap;
  LukeRxId *subs;
  size_t sub_len;
  size_t sub_cap;
} LukeRxNode;

typedef struct LukeRxScopeFrame {
  char name[64];
  uint32_t scope_id;
  LukeRxId *owned;
  size_t owned_len;
  size_t owned_cap;
  int open;
  int error_boundary; /* 1 = component-scoped error containment */
  int boundary_tripped;
  LukeRxId boundary_fault;
} LukeRxScopeFrame;

struct LukeRxGraph {
  LukeArena *arena;
  LukeRxNode *nodes; /* 1-based; index 0 unused */
  size_t len;
  size_t cap;
  LukeRxId *dirty_q;
  size_t dirty_len;
  size_t dirty_cap;
  LukeRxId computing; /* non-zero while a derived/effect recompute is active */
  int batching;
  int epoch;
  int cycle_tripped;
  int stale_read; /* set if compute read a dirty derived — leave dirty & retry */
  int need_paint; /* Phase 2: Argus dirty paint after effect wave */
  int need_layout; /* Phase 2+: Hanka region relayout (text-only skips) */
  LukeRxAfterFlushFn after_flush; /* usually luke_rx_ui_after_flush */
  /* Phase 3 — component scopes */
  LukeRxScopeFrame *scopes;
  size_t scope_len;
  size_t scope_cap;
  uint32_t scope_seq;
  uint32_t active_scope; /* 0 = none */
  int disposed_count;
  int granular_paints; /* Phase 5: count of row/slot paints (tests) */
  /* Phase 11 — Granularity (Milestone C) */
  int region_paints;
  int region_layouts;
  int subtree_invals;
  int last_region_paints_turn;
  /* Phase 12 — Memory management */
  int alive_count;      /* last audit: live nodes */
  int dead_count;       /* last audit: disposed nodes in graph */
  int last_leak_edges;  /* alive nodes with deps on dead nodes */
  int weak_read_count;  /* cumulative weak reads (no dep edge) */
  int scope_gc_count;   /* closed scope frames compacted */
  /* Phase 13 — DevTools */
  LukeRxId last_write_id; /* last cell/collection write (why-changed root hint) */
  int graph_dump_count;
  int why_trace_count;
  /* Phase 14 — Error system */
  int error_count;
  int error_isolation_count;
  int retry_count;
  int async_failure_count;
  LukeRxId last_error_node;
  uint32_t active_boundary; /* innermost open error boundary scope_id */
  int boundary_trip_count;
  uint32_t last_boundary_tripped;
  /* Phase 6 — timelines */
#define LUKE_RX_MAX_TIMELINES 16
  struct {
    char id[64];
    LukeRxId target;
    double from;
    double to;
    int active;
  } timelines[LUKE_RX_MAX_TIMELINES];
  size_t timeline_len;
  /* Phase 9 — correctness / scheduler instrumentation (DevTools prep) */
  int flush_count;           /* completed flush passes */
  int last_flush_derived;    /* derived recomputes in last flush */
  int last_flush_effects;    /* effect runs in last flush */
  int last_flush_deps_cleared; /* stale dep edges removed in last flush */
  int total_deps_cleared;    /* cumulative stale-edge cleanup */
  /* Phase 10 — Scheduler 2.0 */
  int flushing;              /* re-entrancy guard */
  int pending_flush;         /* deferred flush requested during flush */
  int deferred_flush_count;  /* cumulative nested flush deferrals */
  int last_flush_passes;     /* internal passes in last flush turn */
  int last_flush_dedup_hits; /* mark_dirty no-ops last turn */
  int dedup_accum;           /* mark_dirty no-ops accumulating for current turn */
  int last_dirty_q_size;     /* dirty_q length at wave 1 last pass */
  int ui_before_bg;          /* last turn: UI-priority effect before BACKGROUND */
  LukeRxId last_first_effect; /* first effect id run last turn */
  LukeRxId last_last_effect;  /* last effect id run last turn */
#define LUKE_RX_TIMELINE_MAX 64
  struct {
    LukeRxId id;
    uint8_t wave; /* 2 derived, 3 effect */
    uint8_t priority;
  } timeline[LUKE_RX_TIMELINE_MAX];
  size_t sched_timeline_len;
  int last_flush_steps; /* timeline entries last turn */
};

static inline void luke_rx_graph_init(LukeRxGraph *g, LukeArena *a) {
  if (!g) return;
  memset(g, 0, sizeof(*g));
  g->arena = a;
}

static inline LukeRxNode *luke_rx_node(LukeRxGraph *g, LukeRxId id) {
  if (!g || id == 0 || id > g->len) return NULL;
  LukeRxNode *n = &g->nodes[id];
  if (n->dead) return NULL;
  return n;
}

static inline LukeRxNode *luke_rx_node_raw(LukeRxGraph *g, LukeRxId id) {
  if (!g || id == 0 || id > g->len) return NULL;
  return &g->nodes[id];
}

static inline void luke_rx_ensure_nodes(LukeRxGraph *g, size_t need) {
  if (need <= g->cap) return;
  size_t ncap = g->cap ? g->cap : 8;
  while (ncap < need) ncap *= 2;
  LukeRxNode *nn =
      (LukeRxNode *)luke_arena_alloc(g->arena, ncap * sizeof(LukeRxNode), sizeof(void *));
  if (g->nodes && g->cap)
    memcpy(nn, g->nodes, g->cap * sizeof(LukeRxNode));
  memset(nn + g->cap, 0, (ncap - g->cap) * sizeof(LukeRxNode));
  g->nodes = nn;
  g->cap = ncap;
}

static inline int luke_rx_has_edge(LukeRxId *arr, size_t len, LukeRxId v) {
  for (size_t i = 0; i < len; ++i)
    if (arr[i] == v) return 1;
  return 0;
}

static inline void luke_rx_push_id(LukeRxGraph *g, LukeRxId **arr, size_t *len, size_t *cap,
                                   LukeRxId v) {
  if (luke_rx_has_edge(*arr, *len, v)) return;
  if (*len + 1 > *cap) {
    size_t ncap = *cap ? (*cap * 2) : 4;
    LukeRxId *na =
        (LukeRxId *)luke_arena_alloc(g->arena, ncap * sizeof(LukeRxId), sizeof(LukeRxId));
    if (*arr && *len) memcpy(na, *arr, (*len) * sizeof(LukeRxId));
    *arr = na;
    *cap = ncap;
  }
  (*arr)[(*len)++] = v;
}

static inline void luke_rx_scope_track(LukeRxGraph *g, LukeRxId id) {
  if (!g || !g->active_scope || id == 0) return;
  for (size_t i = 0; i < g->scope_len; ++i) {
    LukeRxScopeFrame *s = &g->scopes[i];
    if (!s->open || s->scope_id != g->active_scope) continue;
    luke_rx_push_id(g, &s->owned, &s->owned_len, &s->owned_cap, id);
    LukeRxNode *n = luke_rx_node_raw(g, id);
    if (n) {
      n->scope_id = s->scope_id;
      if (g->active_boundary) n->boundary_scope_id = g->active_boundary;
    }
    return;
  }
}

static inline LukeRxId luke_rx_alloc(LukeRxGraph *g, LukeRxKind kind) {
  if (!g) return 0;
  if (g->len == 0) {
    /* Reserve index 0 as sentinel. */
    luke_rx_ensure_nodes(g, 8);
    memset(&g->nodes[0], 0, sizeof(LukeRxNode));
    g->len = 1;
  }
  if (g->len + 1 > g->cap) luke_rx_ensure_nodes(g, g->len + 1);
  LukeRxId id = (LukeRxId)g->len;
  g->len++;
  LukeRxNode *n = &g->nodes[id];
  memset(n, 0, sizeof(*n));
  n->id = id;
  n->kind = kind;
  n->dirty = 0;
  n->dead = 0;
  luke_rx_scope_track(g, id);
  if (g->active_boundary) {
    LukeRxNode *nn = luke_rx_node_raw(g, id);
    if (nn) nn->boundary_scope_id = g->active_boundary;
  }
  return id;
}

/* dependent `from` reads dependency `to` */
static inline void luke_rx_link(LukeRxGraph *g, LukeRxId from, LukeRxId to) {
  if (!g || from == 0 || to == 0 || from == to) return;
  LukeRxNode *f = luke_rx_node(g, from);
  LukeRxNode *t = luke_rx_node(g, to);
  if (!f || !t) return;
  luke_rx_push_id(g, &f->deps, &f->dep_len, &f->dep_cap, to);
  luke_rx_push_id(g, &t->subs, &t->sub_len, &t->sub_cap, from);
}

static inline void luke_rx_mark_dirty(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n) return;
  if (n->dirty) {
    g->dedup_accum++;
    return;
  }
  n->dirty = 1;
  if (n->queued) return;
  n->queued = 1;
  if (g->dirty_len + 1 > g->dirty_cap) {
    size_t ncap = g->dirty_cap ? g->dirty_cap * 2 : 8;
    LukeRxId *nq =
        (LukeRxId *)luke_arena_alloc(g->arena, ncap * sizeof(LukeRxId), sizeof(LukeRxId));
    if (g->dirty_q && g->dirty_len)
      memcpy(nq, g->dirty_q, g->dirty_len * sizeof(LukeRxId));
    g->dirty_q = nq;
    g->dirty_cap = ncap;
  }
  g->dirty_q[g->dirty_len++] = id;
}

static inline LukeRxId luke_rx_cell(LukeRxGraph *g, double initial) {
  LukeRxId id = luke_rx_alloc(g, LUKE_RX_CELL);
  LukeRxNode *n = luke_rx_node(g, id);
  if (n) {
    n->vkind = LUKE_RX_VAL_NUM;
    n->num = initial;
    n->dirty = 0;
  }
  return id;
}

static inline LukeRxId luke_rx_cell_int(LukeRxGraph *g, int64_t initial) {
  LukeRxId id = luke_rx_alloc(g, LUKE_RX_CELL);
  LukeRxNode *n = luke_rx_node(g, id);
  if (n) {
    n->vkind = LUKE_RX_VAL_INT;
    n->i64 = initial;
    n->num = 0.0;
    n->dirty = 0;
  }
  return id;
}

static inline LukeRxId luke_rx_cell_text(LukeRxGraph *g, LukeText initial) {
  LukeRxId id = luke_rx_alloc(g, LUKE_RX_CELL);
  LukeRxNode *n = luke_rx_node(g, id);
  if (n) {
    n->vkind = LUKE_RX_VAL_TEXT;
    n->text = initial;
    n->dirty = 0;
  }
  return id;
}

static inline LukeRxId luke_rx_list(LukeRxGraph *g) {
  LukeRxId id = luke_rx_alloc(g, LUKE_RX_LIST);
  LukeRxNode *n = luke_rx_node(g, id);
  if (n && g->arena) {
    n->vkind = LUKE_RX_VAL_LIST;
    n->list = luke_list_new(g->arena);
    n->change_kind = 0;
    n->last_index = -1;
    n->dirty = 0;
  }
  return id;
}

static inline LukeRxId luke_rx_map(LukeRxGraph *g) {
  LukeRxId id = luke_rx_alloc(g, LUKE_RX_MAP);
  LukeRxNode *n = luke_rx_node(g, id);
  if (n && g->arena) {
    n->vkind = LUKE_RX_VAL_MAP;
    n->map = luke_map_new(g->arena);
    n->change_kind = 0;
    n->last_index = -1;
    n->dirty = 0;
  }
  return id;
}

static inline LukeRxId luke_rx_derived(LukeRxGraph *g, LukeRxComputeFn fn, void *ctx) {
  LukeRxId id = luke_rx_alloc(g, LUKE_RX_DERIVED);
  LukeRxNode *n = luke_rx_node(g, id);
  if (n) {
    n->vkind = LUKE_RX_VAL_NUM;
    n->priority = LUKE_RX_PRIO_NORMAL;
    n->compute = fn;
    n->ctx = ctx;
    luke_rx_mark_dirty(g, id);
  }
  return id;
}

static inline LukeRxId luke_rx_effect_prio(LukeRxGraph *g, LukeRxEffectFn fn, void *ctx,
                                          LukeRxPriority prio) {
  LukeRxId id = luke_rx_alloc(g, LUKE_RX_EFFECT);
  LukeRxNode *n = luke_rx_node(g, id);
  if (n) {
    n->effect = fn;
    n->ctx = ctx;
    n->priority = (uint8_t)prio;
    luke_rx_mark_dirty(g, id);
  }
  return id;
}

static inline LukeRxId luke_rx_effect(LukeRxGraph *g, LukeRxEffectFn fn, void *ctx) {
  return luke_rx_effect_prio(g, fn, ctx, LUKE_RX_PRIO_UI);
}

static inline LukeRxId luke_rx_effect_weak(LukeRxGraph *g, LukeRxEffectFn fn, void *ctx,
                                           LukeRxPriority prio) {
  LukeRxId id = luke_rx_effect_prio(g, fn, ctx, prio);
  LukeRxNode *n = luke_rx_node_raw(g, id);
  if (n) n->weak = 1;
  return id;
}

static inline int luke_rx_computing_weak(LukeRxGraph *g) {
  if (!g || !g->computing) return 0;
  LukeRxNode *n = luke_rx_node_raw(g, g->computing);
  return n && n->weak;
}

static inline void luke_rx_remove_id(LukeRxId *arr, size_t *len, LukeRxId v) {
  if (!arr || !len) return;
  size_t w = 0;
  for (size_t i = 0; i < *len; ++i) {
    if (arr[i] == v) continue;
    arr[w++] = arr[i];
  }
  *len = w;
}

/* Drop dynamic deps for `from` and unlink from each dependency's subs list. */
static inline void luke_rx_clear_deps(LukeRxGraph *g, LukeRxId from) {
  LukeRxNode *n = luke_rx_node_raw(g, from);
  if (!n || !g) return;
  for (size_t i = 0; i < n->dep_len; ++i) {
    LukeRxNode *d = luke_rx_node_raw(g, n->deps[i]);
    if (d) luke_rx_remove_id(d->subs, &d->sub_len, from);
    g->last_flush_deps_cleared++;
    g->total_deps_cleared++;
  }
  n->dep_len = 0;
}

static inline void luke_rx_invalidate_subs(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n) return;
  for (size_t i = 0; i < n->sub_len; ++i) {
    LukeRxId s = n->subs[i];
    LukeRxNode *sn = luke_rx_node(g, s);
    if (!sn) continue;
    if (!sn->dirty) {
      luke_rx_mark_dirty(g, s);
      luke_rx_invalidate_subs(g, s);
    }
  }
}

static inline void luke_rx_timeline_push(LukeRxGraph *g, LukeRxId id, uint8_t wave,
                                         uint8_t priority) {
  if (!g || g->sched_timeline_len >= LUKE_RX_TIMELINE_MAX) return;
  g->timeline[g->sched_timeline_len].id = id;
  g->timeline[g->sched_timeline_len].wave = wave;
  g->timeline[g->sched_timeline_len].priority = priority;
  g->sched_timeline_len++;
}

static inline void luke_rx_request_flush(LukeRxGraph *g); /* forward */

static inline LukeRxScopeFrame *luke_rx_boundary_frame(LukeRxGraph *g, uint32_t boundary_scope_id) {
  if (!g || boundary_scope_id == 0) return NULL;
  for (size_t i = 0; i < g->scope_len; ++i) {
    LukeRxScopeFrame *s = &g->scopes[i];
    if (s->scope_id == boundary_scope_id && s->error_boundary) return s;
  }
  return NULL;
}

static inline LukeRxScopeFrame *luke_rx_boundary_find(LukeRxGraph *g, const char *name) {
  if (!g || !name) return NULL;
  for (size_t i = g->scope_len; i > 0; --i) {
    LukeRxScopeFrame *s = &g->scopes[i - 1];
    if (s->error_boundary && strcmp(s->name, name) == 0) return s;
  }
  return NULL;
}

static inline void luke_rx_sync_active_boundary(LukeRxGraph *g) {
  if (!g) return;
  g->active_boundary = 0;
  for (size_t i = g->scope_len; i > 0; --i) {
    LukeRxScopeFrame *s = &g->scopes[i - 1];
    if (s->open && s->error_boundary && !s->boundary_tripped) {
      g->active_boundary = s->scope_id;
      break;
    }
  }
}

static inline int luke_rx_boundary_blocks(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node_raw(g, id);
  if (!n || n->boundary_scope_id == 0) return 0;
  LukeRxScopeFrame *b = luke_rx_boundary_frame(g, n->boundary_scope_id);
  return b && b->boundary_tripped ? 1 : 0;
}

static inline void luke_rx_trip_boundary(LukeRxGraph *g, LukeRxId fault_id) {
  LukeRxNode *n = luke_rx_node_raw(g, fault_id);
  if (!n || n->boundary_scope_id == 0) return;
  LukeRxScopeFrame *b = luke_rx_boundary_frame(g, n->boundary_scope_id);
  if (!b || b->boundary_tripped) return;
  b->boundary_tripped = 1;
  b->boundary_fault = fault_id;
  g->boundary_trip_count++;
  g->last_boundary_tripped = b->scope_id;
  fprintf(stderr, "Luke Reactive: error boundary '%s' tripped on node #%u\n", b->name,
          (unsigned)fault_id);
}

static inline void luke_rx_isolate_error(LukeRxGraph *g, LukeRxId id) {
  if (!g || id == 0) return;
  LukeRxNode *n = luke_rx_node_raw(g, id);
  if (!n || n->dead) return;
  n->errored = 1;
  g->last_error_node = id;
  g->error_count++;
  g->error_isolation_count++;
  luke_rx_clear_deps(g, id);
  n->dirty = 0;
  n->queued = 0;
  luke_rx_trip_boundary(g, id);
  fprintf(stderr, "Luke Reactive: isolated error on node #%u\n", (unsigned)id);
}

static inline void luke_rx_report_async_failure(LukeRxGraph *g, LukeRxId id, LukeText msg) {
  if (!g) return;
  luke_set_problem(msg);
  g->async_failure_count++;
  luke_rx_isolate_error(g, id);
  luke_clear_problem();
}

static inline int luke_rx_retry_error(LukeRxGraph *g) {
  if (!g || g->last_error_node == 0) return 0;
  LukeRxNode *n = luke_rx_node_raw(g, g->last_error_node);
  if (!n || n->dead) return 0;
  n->errored = 0;
  g->retry_count++;
  luke_rx_mark_dirty(g, g->last_error_node);
  if (!g->batching) luke_rx_request_flush(g);
  return 1;
}

static inline int luke_rx_clear_reactive_error(LukeRxGraph *g) {
  if (!g || g->last_error_node == 0) return 0;
  LukeRxNode *n = luke_rx_node_raw(g, g->last_error_node);
  if (!n) return 0;
  n->errored = 0;
  return 1;
}

static inline int luke_rx_node_errored(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node_raw(g, id);
  return n && n->errored ? 1 : 0;
}

static inline int luke_rx_flush(LukeRxGraph *g); /* forward */

static inline int luke_rx_flush_pass(LukeRxGraph *g) {
  if (!g || g->dirty_len == 0) return 0;
  g->cycle_tripped = 0;
  g->last_flush_derived = 0;
  g->last_flush_effects = 0;
  g->last_flush_deps_cleared = 0;
  g->last_dirty_q_size = (int)g->dirty_len;
  g->sched_timeline_len = 0;
  g->ui_before_bg = 0;
  g->last_first_effect = 0;
  g->last_last_effect = 0;

  /* Wave 1 — invalidate already queued; ensure transitive dirty marks. */
  for (size_t i = 0; i < g->dirty_len; ++i)
    luke_rx_invalidate_subs(g, g->dirty_q[i]);

  /* Wave 2 — recompute pure derived (ascending id order = deterministic). */
  int guard = (int)(g->len * 4 + 8);
  while (guard-- > 0) {
    int progressed = 0;
    int pending_derived = 0;
    for (LukeRxId id = 1; id < (LukeRxId)g->len; ++id) {
      LukeRxNode *n = &g->nodes[id];
      if (n->dead || n->errored || !n->dirty || n->kind != LUKE_RX_DERIVED) continue;
      if (luke_rx_boundary_blocks(g, id)) {
        n->dirty = 0;
        n->queued = 0;
        continue;
      }
      pending_derived = 1;
      int deps_ready = 1;
      for (size_t d = 0; d < n->dep_len; ++d) {
        LukeRxNode *dn = luke_rx_node(g, n->deps[d]);
        if (dn && dn->dirty && dn->kind == LUKE_RX_DERIVED) {
          deps_ready = 0;
          break;
        }
      }
      if (!deps_ready) continue;
      if (!n->compute) {
        n->dirty = 0;
        n->queued = 0;
        progressed = 1;
        continue;
      }
      luke_rx_clear_deps(g, id);
      g->computing = id;
      g->stale_read = 0;
      double v = n->compute(g, n->ctx);
      g->computing = 0;
      if (luke_has_problem()) {
        luke_rx_isolate_error(g, id);
        luke_clear_problem();
        progressed = 1;
        continue;
      }
      if (g->stale_read) {
        g->stale_read = 0;
        continue;
      }
      n->num = v;
      n->version++;
      n->dirty = 0;
      n->queued = 0;
      g->last_flush_derived++;
      luke_rx_timeline_push(g, id, 2, n->priority);
      progressed = 1;
    }
    if (!pending_derived) break;
    if (!progressed) {
      g->cycle_tripped = 1;
      fprintf(stderr, "Luke Reactive: cycle detected in derived graph (epoch %d)\n", g->epoch);
      for (LukeRxId id = 1; id < (LukeRxId)g->len; ++id) {
        if (!g->nodes[id].dead && g->nodes[id].kind == LUKE_RX_DERIVED)
          g->nodes[id].dirty = 0;
      }
      break;
    }
  }

  /* Wave 3 — effects (priority lane, then ascending id). */
#define LUKE_RX_MAX_EFFECT_BATCH 128
  LukeRxId effect_ids[LUKE_RX_MAX_EFFECT_BATCH];
  int effect_pri[LUKE_RX_MAX_EFFECT_BATCH];
  size_t effect_n = 0;
  for (LukeRxId id = 1; id < (LukeRxId)g->len && effect_n < LUKE_RX_MAX_EFFECT_BATCH; ++id) {
    LukeRxNode *n = &g->nodes[id];
    if (n->dead || n->errored || !n->dirty || n->kind != LUKE_RX_EFFECT) continue;
    if (luke_rx_boundary_blocks(g, id)) {
      n->dirty = 0;
      n->queued = 0;
      continue;
    }
    n->wait_epochs++;
    effect_ids[effect_n] = id;
    effect_pri[effect_n] = (int)n->priority;
    if (n->wait_epochs >= 3 && effect_pri[effect_n] > (int)LUKE_RX_PRIO_UI)
      effect_pri[effect_n] = (int)LUKE_RX_PRIO_UI;
    effect_n++;
  }
  for (size_t i = 0; i + 1 < effect_n; ++i) {
    for (size_t j = i + 1; j < effect_n; ++j) {
      int swap = 0;
      if (effect_pri[j] < effect_pri[i]) swap = 1;
      else if (effect_pri[j] == effect_pri[i] && effect_ids[j] < effect_ids[i]) swap = 1;
      if (!swap) continue;
      int tp = effect_pri[i];
      effect_pri[i] = effect_pri[j];
      effect_pri[j] = tp;
      LukeRxId tid = effect_ids[i];
      effect_ids[i] = effect_ids[j];
      effect_ids[j] = tid;
    }
  }
  int saw_ui = 0;
  for (size_t ei = 0; ei < effect_n; ++ei) {
    LukeRxId id = effect_ids[ei];
    LukeRxNode *n = &g->nodes[id];
    if (n->dead || n->errored || !n->dirty || n->kind != LUKE_RX_EFFECT) continue;
    if (luke_rx_boundary_blocks(g, id)) {
      n->dirty = 0;
      n->queued = 0;
      continue;
    }
    if (n->priority == LUKE_RX_PRIO_BACKGROUND && saw_ui) g->ui_before_bg = 1;
    if (n->priority == LUKE_RX_PRIO_UI) saw_ui = 1;
    if (!g->last_first_effect) g->last_first_effect = id;
    g->last_last_effect = id;
    luke_rx_clear_deps(g, id);
    g->computing = id;
    if (n->effect) n->effect(g, n->ctx);
    g->computing = 0;
    if (luke_has_problem()) {
      luke_rx_isolate_error(g, id);
      luke_clear_problem();
      continue;
    }
    n->version++;
    n->dirty = 0;
    n->queued = 0;
    n->wait_epochs = 0;
    g->last_flush_effects++;
    luke_rx_timeline_push(g, id, 3, n->priority);
  }

  /* Clear remaining value dirty flags (cells / collections hold values already). */
  for (LukeRxId id = 1; id < (LukeRxId)g->len; ++id) {
    if (g->nodes[id].dead) continue;
    LukeRxKind k = g->nodes[id].kind;
    if (k == LUKE_RX_CELL || k == LUKE_RX_LIST || k == LUKE_RX_MAP) {
      g->nodes[id].dirty = 0;
      g->nodes[id].queued = 0;
    }
  }
  g->dirty_len = 0;
  g->last_flush_steps = (int)g->sched_timeline_len;
  return g->cycle_tripped ? -1 : 1;
}

static inline void luke_rx_request_flush(LukeRxGraph *g) {
  if (!g || g->dirty_len == 0) return;
  if (g->flushing) {
    g->pending_flush = 1;
    g->deferred_flush_count++;
    return;
  }
  luke_rx_flush(g);
}

static inline int luke_rx_flush(LukeRxGraph *g) {
  if (!g || g->dirty_len == 0) return 0;
  /* Per-flush counters — THE REGION PAINT COUNT reads region_paints after the
   * wave, so a single-node BIND update reports 1 (not a cumulative total). */
  g->region_paints = 0;
  g->region_layouts = 0;
  g->flushing = 1;
  int result = 1;
  int passes = 0;
  do {
    g->pending_flush = 0;
    result = luke_rx_flush_pass(g);
    passes++;
  } while (g->pending_flush && g->dirty_len > 0);
  g->flushing = 0;
  g->last_flush_passes = passes;
  g->last_flush_dedup_hits = g->dedup_accum;
  g->dedup_accum = 0;
  g->flush_count++;
  g->epoch++;
  if (g->after_flush) g->after_flush(g);
  return g->cycle_tripped ? -1 : result;
}

static inline void luke_rx_batch_begin(LukeRxGraph *g) {
  if (g) g->batching++;
}

static inline void luke_rx_batch_end(LukeRxGraph *g) {
  if (!g) return;
  if (g->batching > 0) g->batching--;
  if (g->batching == 0) luke_rx_request_flush(g);
}

static inline void luke_rx_write_num(LukeRxGraph *g, LukeRxId id, double v) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n || n->kind != LUKE_RX_CELL) return;
  if (n->vkind == LUKE_RX_VAL_TEXT || n->vkind == LUKE_RX_VAL_INT) return;
  if (n->num == v && !n->dirty) return;
  n->num = v;
  n->version++;
  g->last_write_id = id;
  luke_rx_mark_dirty(g, id);
  luke_rx_invalidate_subs(g, id);
  if (!g->batching) luke_rx_request_flush(g);
}

static inline void luke_rx_write_int(LukeRxGraph *g, LukeRxId id, int64_t v) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n || n->kind != LUKE_RX_CELL) return;
  if (n->vkind != LUKE_RX_VAL_INT) {
    if (n->vkind == LUKE_RX_VAL_TEXT) return;
    n->vkind = LUKE_RX_VAL_INT;
  }
  if (n->i64 == v && !n->dirty) return;
  n->i64 = v;
  n->version++;
  g->last_write_id = id;
  luke_rx_mark_dirty(g, id);
  luke_rx_invalidate_subs(g, id);
  if (!g->batching) luke_rx_request_flush(g);
}

static inline int luke_rx_text_eq(LukeText a, LukeText b) {
  return a.len == b.len && (a.len == 0 || (a.ptr && b.ptr && memcmp(a.ptr, b.ptr, a.len) == 0));
}

static inline void luke_rx_touch_coll(LukeRxGraph *g, LukeRxId id, int change_kind, int last_index) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n) return;
  n->change_kind = change_kind;
  n->last_index = last_index;
  n->version++;
  g->last_write_id = id;
  luke_rx_mark_dirty(g, id);
  luke_rx_invalidate_subs(g, id);
  if (!g->batching) luke_rx_request_flush(g);
}

static inline void luke_rx_list_add(LukeRxGraph *g, LukeRxId id, LukeText v) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n || n->kind != LUKE_RX_LIST || !n->list || !g->arena) return;
  luke_list_add(g->arena, n->list, v);
  luke_rx_touch_coll(g, id, 1, (int)n->list->len - 1);
}

static inline void luke_rx_list_set(LukeRxGraph *g, LukeRxId id, double index, LukeText v) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n || n->kind != LUKE_RX_LIST || !n->list) return;
  if (index < 0 || (size_t)index >= n->list->len) return;
  LukeText cur = luke_list_get(n->list, index);
  if (luke_rx_text_eq(cur, v)) return;
  luke_list_set(n->list, index, v);
  luke_rx_touch_coll(g, id, 2, (int)index);
}

static inline LukeText luke_rx_list_get(LukeRxGraph *g, LukeRxId id, double index) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n || n->kind != LUKE_RX_LIST) return luke_text("");
  if (g->computing) luke_rx_link(g, g->computing, id);
  return luke_list_get(n->list, index);
}

static inline double luke_rx_list_len(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n || n->kind != LUKE_RX_LIST) return 0;
  if (g->computing) luke_rx_link(g, g->computing, id);
  return luke_list_len(n->list);
}

static inline void luke_rx_map_put(LukeRxGraph *g, LukeRxId id, LukeText key, LukeText val) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n || n->kind != LUKE_RX_MAP || !n->map || !g->arena) return;
  int existed = luke_map_has(n->map, key);
  luke_map_put(g->arena, n->map, key, val);
  if (!existed)
    luke_rx_touch_coll(g, id, 1, (int)n->map->len - 1);
  else {
    int slot = -1;
    for (size_t i = 0; i < n->map->len; ++i) {
      if (luke_map_key_eq(n->map->keys[i], key)) {
        slot = (int)i;
        break;
      }
    }
    luke_rx_touch_coll(g, id, 2, slot);
  }
}

static inline LukeText luke_rx_map_get(LukeRxGraph *g, LukeRxId id, LukeText key) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n || n->kind != LUKE_RX_MAP) return luke_text("");
  if (g->computing) luke_rx_link(g, g->computing, id);
  return luke_map_get(n->map, key);
}

static inline double luke_rx_map_len(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n || n->kind != LUKE_RX_MAP) return 0;
  if (g->computing) luke_rx_link(g, g->computing, id);
  return luke_map_len(n->map);
}

static inline LukeList *luke_rx_list_ptr(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n || n->kind != LUKE_RX_LIST) return NULL;
  if (g->computing) luke_rx_link(g, g->computing, id);
  return n->list;
}

static inline LukeMap *luke_rx_map_ptr(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n || n->kind != LUKE_RX_MAP) return NULL;
  if (g->computing) luke_rx_link(g, g->computing, id);
  return n->map;
}

static inline void luke_rx_write_text(LukeRxGraph *g, LukeRxId id, LukeText v) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n || n->kind != LUKE_RX_CELL) return;
  if (n->vkind != LUKE_RX_VAL_TEXT) {
    n->vkind = LUKE_RX_VAL_TEXT;
  }
  if (luke_rx_text_eq(n->text, v) && !n->dirty) return;
  n->text = v;
  n->version++;
  g->last_write_id = id;
  luke_rx_mark_dirty(g, id);
  luke_rx_invalidate_subs(g, id);
  if (!g->batching) luke_rx_request_flush(g);
}

static inline double luke_rx_read_num(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n) return 0.0;
  if (g->computing) {
    if (luke_rx_computing_weak(g))
      g->weak_read_count++;
    else {
      luke_rx_link(g, g->computing, id);
      if (n->dirty && n->kind == LUKE_RX_DERIVED) g->stale_read = 1;
    }
  }
  if (n->dirty && n->kind == LUKE_RX_DERIVED && !g->computing) luke_rx_request_flush(g);
  n = luke_rx_node(g, id);
  if (!n) return 0.0;
  if (n->vkind == LUKE_RX_VAL_INT) return (double)n->i64;
  return n->num;
}

static inline int64_t luke_rx_read_int(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n) return 0;
  if (g->computing) {
    if (luke_rx_computing_weak(g))
      g->weak_read_count++;
    else {
      luke_rx_link(g, g->computing, id);
      if (n->dirty && n->kind == LUKE_RX_DERIVED) g->stale_read = 1;
    }
  }
  if (n->dirty && n->kind == LUKE_RX_DERIVED && !g->computing) luke_rx_request_flush(g);
  n = luke_rx_node(g, id);
  if (!n) return 0;
  if (n->vkind == LUKE_RX_VAL_INT) return n->i64;
  return (int64_t)n->num;
}

/* Weak read — observe value without registering a dependency edge. */
static inline double luke_rx_read_num_weak(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n) return 0.0;
  if (g->computing) g->weak_read_count++;
  if (n->dirty && n->kind == LUKE_RX_DERIVED && !g->computing) luke_rx_request_flush(g);
  n = luke_rx_node(g, id);
  if (!n) return 0.0;
  if (n->vkind == LUKE_RX_VAL_INT) return (double)n->i64;
  return n->num;
}

static inline int64_t luke_rx_read_int_weak(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n) return 0;
  if (g->computing) g->weak_read_count++;
  if (n->dirty && n->kind == LUKE_RX_DERIVED && !g->computing) luke_rx_request_flush(g);
  n = luke_rx_node(g, id);
  if (!n) return 0;
  if (n->vkind == LUKE_RX_VAL_INT) return n->i64;
  return (int64_t)n->num;
}

static inline LukeText luke_rx_read_text_weak(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n) return luke_text("");
  if (g->computing) g->weak_read_count++;
  if (n->dirty && n->kind == LUKE_RX_DERIVED && !g->computing) luke_rx_request_flush(g);
  n = luke_rx_node(g, id);
  if (!n) return luke_text("");
  if (n->vkind == LUKE_RX_VAL_TEXT) return n->text;
  if (g->arena) {
    char buf[64];
    int k = snprintf(buf, sizeof(buf), "%.10g", n->num);
    if (k < 0) k = 0;
    char *p = (char *)luke_arena_alloc(g->arena, (size_t)k + 1, 1);
    memcpy(p, buf, (size_t)k + 1);
    return luke_text_n(p, (size_t)k);
  }
  return luke_text("");
}

static inline LukeText luke_rx_read_text(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n) return luke_text("");
  if (g->computing) {
    if (luke_rx_computing_weak(g))
      g->weak_read_count++;
    else {
      luke_rx_link(g, g->computing, id);
      if (n->dirty && n->kind == LUKE_RX_DERIVED) g->stale_read = 1;
    }
  }
  if (n->dirty && n->kind == LUKE_RX_DERIVED && !g->computing) luke_rx_request_flush(g);
  n = luke_rx_node(g, id);
  if (!n) return luke_text("");
  if (n->vkind == LUKE_RX_VAL_TEXT) return n->text;
  /* Number cell read as text — allocate via arena when available. */
  if (g->arena) {
    char buf[64];
    int k = snprintf(buf, sizeof(buf), "%.10g", n->num);
    if (k < 0) k = 0;
    char *p = (char *)luke_arena_alloc(g->arena, (size_t)k + 1, 1);
    memcpy(p, buf, (size_t)k + 1);
    return luke_text_n(p, (size_t)k);
  }
  return luke_text("");
}

static inline int luke_rx_version(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  return n ? n->version : 0;
}

static inline int luke_rx_is_dirty(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  return n ? n->dirty : 0;
}

static inline int luke_rx_is_alive(LukeRxGraph *g, LukeRxId id) {
  return luke_rx_node(g, id) != NULL;
}

static inline int luke_rx_alive_count(LukeRxGraph *g) {
  if (!g) return 0;
  int n = 0;
  for (LukeRxId id = 1; id < (LukeRxId)g->len; ++id)
    if (luke_rx_node(g, id)) n++;
  return n;
}

static inline int luke_rx_dead_count(LukeRxGraph *g) {
  if (!g) return 0;
  int n = 0;
  for (LukeRxId id = 1; id < (LukeRxId)g->len; ++id) {
    LukeRxNode *raw = luke_rx_node_raw(g, id);
    if (raw && raw->dead) n++;
  }
  return n;
}

/* Remove dependency edges from alive nodes to dead nodes. */
static inline int luke_rx_repair_leaks(LukeRxGraph *g) {
  if (!g) return 0;
  int fixed = 0;
  for (LukeRxId id = 1; id < (LukeRxId)g->len; ++id) {
    LukeRxNode *n = luke_rx_node(g, id);
    if (!n) continue;
    for (size_t i = 0; i < n->dep_len;) {
      LukeRxNode *d = luke_rx_node_raw(g, n->deps[i]);
      if (d && !d->dead) {
        i++;
        continue;
      }
      LukeRxId dead_id = n->deps[i];
      luke_rx_remove_id(n->deps, &n->dep_len, dead_id);
      LukeRxNode *dr = luke_rx_node_raw(g, dead_id);
      if (dr) luke_rx_remove_id(dr->subs, &dr->sub_len, id);
      fixed++;
    }
  }
  return fixed;
}

/* Scan graph for stale edges; repair and refresh memory counters. */
static inline int luke_rx_audit_graph(LukeRxGraph *g) {
  if (!g) return 0;
  int leaks = 0;
  g->alive_count = 0;
  g->dead_count = 0;
  for (LukeRxId id = 1; id < (LukeRxId)g->len; ++id) {
    LukeRxNode *raw = luke_rx_node_raw(g, id);
    if (!raw || raw->id == 0) continue;
    if (raw->dead) {
      g->dead_count++;
      continue;
    }
    g->alive_count++;
    for (size_t d = 0; d < raw->dep_len; ++d) {
      LukeRxNode *dn = luke_rx_node_raw(g, raw->deps[d]);
      if (!dn || dn->dead) leaks++;
    }
  }
  g->last_leak_edges = leaks;
  if (leaks > 0) luke_rx_repair_leaks(g);
  return leaks;
}

static inline void luke_rx_dispose_node(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node_raw(g, id);
  if (!n || n->dead) return;
  /* Unlink from neighbors. */
  for (size_t i = 0; i < n->dep_len; ++i) {
    LukeRxNode *d = luke_rx_node_raw(g, n->deps[i]);
    if (d) luke_rx_remove_id(d->subs, &d->sub_len, id);
  }
  for (size_t i = 0; i < n->sub_len; ++i) {
    LukeRxNode *s = luke_rx_node_raw(g, n->subs[i]);
    if (s) luke_rx_remove_id(s->deps, &s->dep_len, id);
  }
  n->dep_len = 0;
  n->sub_len = 0;
  n->compute = NULL;
  n->effect = NULL;
  n->ctx = NULL;
  n->dirty = 0;
  n->dead = 1;
  g->disposed_count++;
}

static inline int luke_rx_scope_begin(LukeRxGraph *g, const char *name) {
  if (!g) return 0;
  if (g->scope_len + 1 > g->scope_cap) {
    size_t ncap = g->scope_cap ? g->scope_cap * 2 : 4;
    LukeRxScopeFrame *ns = (LukeRxScopeFrame *)luke_arena_alloc(
        g->arena, ncap * sizeof(LukeRxScopeFrame), sizeof(void *));
    if (g->scopes && g->scope_len)
      memcpy(ns, g->scopes, g->scope_len * sizeof(LukeRxScopeFrame));
    memset(ns + g->scope_cap, 0, (ncap - g->scope_cap) * sizeof(LukeRxScopeFrame));
    g->scopes = ns;
    g->scope_cap = ncap;
  }
  LukeRxScopeFrame *s = &g->scopes[g->scope_len++];
  memset(s, 0, sizeof(*s));
  if (name && name[0]) {
    size_t n = strlen(name);
    if (n >= sizeof(s->name)) n = sizeof(s->name) - 1;
    memcpy(s->name, name, n);
    s->name[n] = '\0';
  } else {
    snprintf(s->name, sizeof(s->name), "scope%u", g->scope_seq + 1);
  }
  s->scope_id = ++g->scope_seq;
  s->open = 1;
  g->active_scope = s->scope_id;
  return 1;
}

static inline LukeRxScopeFrame *luke_rx_scope_find(LukeRxGraph *g, const char *name) {
  if (!g || !name) return NULL;
  for (size_t i = g->scope_len; i > 0; --i) {
    LukeRxScopeFrame *s = &g->scopes[i - 1];
    if (s->open && strcmp(s->name, name) == 0) return s;
  }
  return NULL;
}

static inline int luke_rx_boundary_begin(LukeRxGraph *g, const char *name) {
  if (!luke_rx_scope_begin(g, name)) return 0;
  LukeRxScopeFrame *s = luke_rx_scope_find(g, name);
  if (!s) return 0;
  s->error_boundary = 1;
  g->active_boundary = s->scope_id;
  return 1;
}

static inline void luke_rx_boundary_end(LukeRxGraph *g, const char *name) {
  if (!g) return;
  LukeRxScopeFrame *s = luke_rx_boundary_find(g, name);
  if (!s || !s->open) return;
  g->active_scope = 0;
  for (size_t i = g->scope_len; i > 0; --i) {
    LukeRxScopeFrame *f = &g->scopes[i - 1];
    if (f->open && !f->error_boundary) {
      g->active_scope = f->scope_id;
      break;
    }
  }
  g->active_boundary = 0;
  for (size_t i = g->scope_len; i > 0; --i) {
    LukeRxScopeFrame *f = &g->scopes[i - 1];
    if (f->open && f->error_boundary && f->scope_id != s->scope_id && !f->boundary_tripped) {
      g->active_boundary = f->scope_id;
      break;
    }
  }
}

static inline int luke_rx_boundary_reset(LukeRxGraph *g, const char *name) {
  if (!g) return 0;
  LukeRxScopeFrame *s = luke_rx_boundary_find(g, name);
  if (!s) return 0;
  s->boundary_tripped = 0;
  s->boundary_fault = 0;
  if (g->last_boundary_tripped == s->scope_id) g->last_boundary_tripped = 0;
  for (LukeRxId id = 1; id < (LukeRxId)g->len; ++id) {
    LukeRxNode *n = luke_rx_node_raw(g, id);
    if (!n || n->dead || n->boundary_scope_id != s->scope_id) continue;
    if (n->errored) {
      n->errored = 0;
      luke_rx_mark_dirty(g, id);
    }
  }
  luke_rx_sync_active_boundary(g);
  return 1;
}

static inline int luke_rx_boundary_tripped(LukeRxGraph *g, const char *name) {
  LukeRxScopeFrame *s = luke_rx_boundary_find(g, name);
  return s && s->boundary_tripped ? 1 : 0;
}

/* END COMPONENT — stop tracking new nodes in this scope; frame stays for DESTROY. */
static inline void luke_rx_scope_pause(LukeRxGraph *g) {
  if (!g) return;
  g->active_scope = 0;
}

/* Drop closed scope frames with no owned nodes (unmounted UI cleanup). */
static inline int luke_rx_scope_gc(LukeRxGraph *g) {
  if (!g || g->scope_len == 0) return 0;
  int removed = 0;
  size_t w = 0;
  for (size_t i = 0; i < g->scope_len; ++i) {
    LukeRxScopeFrame *s = &g->scopes[i];
    if (!s->open && s->owned_len == 0) {
      removed++;
      continue;
    }
    if (w != i) g->scopes[w] = g->scopes[i];
    w++;
  }
  g->scope_len = w;
  g->scope_gc_count += removed;
  return removed;
}

/* Destroy a named component scope: unsubscribe owned cells/effects. */
static inline int luke_rx_scope_end(LukeRxGraph *g, const char *name) {
  if (!g) return 0;
  LukeRxScopeFrame *s = NULL;
  if (name && name[0])
    s = luke_rx_scope_find(g, name);
  else {
    for (size_t i = g->scope_len; i > 0; --i) {
      if (g->scopes[i - 1].open) {
        s = &g->scopes[i - 1];
        break;
      }
    }
  }
  if (!s || !s->open) return 0;
  for (size_t i = 0; i < s->owned_len; ++i) {
    LukeRxId oid = s->owned[i];
    LukeRxNode *on = luke_rx_node_raw(g, oid);
    if (!on) continue;
    for (size_t j = 0; j < on->sub_len; ++j) {
      LukeRxId sub = on->subs[j];
      int owned = 0;
      for (size_t k = 0; k < s->owned_len; ++k)
        if (s->owned[k] == sub) {
          owned = 1;
          break;
        }
      if (owned) continue;
      if (luke_rx_node(g, sub)) {
        luke_rx_mark_dirty(g, sub);
        g->subtree_invals++;
      }
    }
  }
  for (size_t i = 0; i < s->owned_len; ++i)
    luke_rx_dispose_node(g, s->owned[i]);
  s->owned_len = 0;
  s->open = 0;
  luke_rx_audit_graph(g);
  luke_rx_scope_gc(g);
  /* Restore active_scope to nearest still-open frame. */
  g->active_scope = 0;
  for (size_t i = g->scope_len; i > 0; --i) {
    if (g->scopes[i - 1].open) {
      g->active_scope = g->scopes[i - 1].scope_id;
      break;
    }
  }
  return 1;
}

static inline int luke_rx_scope_alive(LukeRxGraph *g, const char *name) {
  LukeRxScopeFrame *s = luke_rx_scope_find(g, name);
  return s && s->open ? 1 : 0;
}

static inline size_t luke_rx_scope_owned(LukeRxGraph *g, const char *name) {
  LukeRxScopeFrame *s = luke_rx_scope_find(g, name);
  return s ? s->owned_len : 0;
}

static inline size_t luke_rx_scope_frame_count(LukeRxGraph *g) {
  return g ? g->scope_len : 0;
}

/* ---------- Phase 13 DevTools ---------- */

static inline int luke_rx_is_source_kind(LukeRxKind k) {
  return k == LUKE_RX_CELL || k == LUKE_RX_LIST || k == LUKE_RX_MAP;
}

static inline size_t luke_rx_dep_count(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  return n ? n->dep_len : 0;
}

static inline size_t luke_rx_sub_count(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  return n ? n->sub_len : 0;
}

static inline int luke_rx_count_kind(LukeRxGraph *g, LukeRxKind kind) {
  if (!g) return 0;
  int c = 0;
  for (LukeRxId id = 1; id < (LukeRxId)g->len; ++id) {
    LukeRxNode *n = luke_rx_node(g, id);
    if (n && n->kind == kind) c++;
  }
  return c;
}

static inline int luke_rx_edge_count(LukeRxGraph *g) {
  if (!g) return 0;
  int c = 0;
  for (LukeRxId id = 1; id < (LukeRxId)g->len; ++id) {
    LukeRxNode *n = luke_rx_node(g, id);
    if (n) c += (int)n->dep_len;
  }
  return c;
}

static inline LukeRxId luke_rx_timeline_step_id(LukeRxGraph *g, size_t index) {
  if (!g || index >= g->sched_timeline_len) return 0;
  return g->timeline[index].id;
}

static inline int luke_rx_timeline_step_wave(LukeRxGraph *g, size_t index) {
  if (!g || index >= g->sched_timeline_len) return 0;
  return (int)g->timeline[index].wave;
}

/* Nearest source cell/collection upstream (BFS, min id tie-break). */
static inline LukeRxId luke_rx_why_root(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *start = luke_rx_node(g, id);
  if (!start) return 0;
  if (luke_rx_is_source_kind(start->kind)) return id;
  LukeRxId q[64];
  int depth[64];
  size_t head = 0, tail = 0;
  q[tail] = id;
  depth[tail++] = 0;
  LukeRxId best = 0;
  int best_depth = 999999;
  while (head < tail && head < 64) {
    LukeRxId cur = q[head];
    int d = depth[head++];
    LukeRxNode *n = luke_rx_node(g, cur);
    if (!n) continue;
    if (luke_rx_is_source_kind(n->kind)) {
      if (d < best_depth || (d == best_depth && (best == 0 || cur < best))) {
        best = cur;
        best_depth = d;
      }
      continue;
    }
    for (size_t i = 0; i < n->dep_len && tail < 64; ++i) {
      LukeRxId dep = n->deps[i];
      if (!luke_rx_node(g, dep)) continue;
      q[tail] = dep;
      depth[tail++] = d + 1;
    }
  }
  return best ? best : id;
}

static inline int luke_rx_why_depth(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *start = luke_rx_node(g, id);
  if (!start) return 0;
  if (luke_rx_is_source_kind(start->kind)) return 0;
  LukeRxId root = luke_rx_why_root(g, id);
  if (root == 0 || root == id) return 0;
  LukeRxId q[64];
  int depth[64];
  size_t head = 0, tail = 0;
  q[tail] = id;
  depth[tail++] = 0;
  while (head < tail && head < 64) {
    LukeRxId cur = q[head];
    int d = depth[head++];
    if (cur == root) return d;
    LukeRxNode *n = luke_rx_node(g, cur);
    if (!n) continue;
    for (size_t i = 0; i < n->dep_len && tail < 64; ++i) {
      LukeRxId dep = n->deps[i];
      if (!luke_rx_node(g, dep)) continue;
      q[tail] = dep;
      depth[tail++] = d + 1;
    }
  }
  return 0;
}

static inline void luke_rx_dump_graph(LukeRxGraph *g) {
  if (!g) return;
  g->graph_dump_count++;
  for (LukeRxId id = 1; id < (LukeRxId)g->len; ++id) {
    LukeRxNode *n = luke_rx_node(g, id);
    if (!n) continue;
    fprintf(stderr, "rx#%u name=%s kind=%d deps=%zu subs=%zu dirty=%d v=%.10g\n", (unsigned)id,
            n->name[0] ? n->name : "-", (int)n->kind, n->dep_len, n->sub_len, n->dirty, n->num);
  }
}

static inline void luke_rx_set_name(LukeRxGraph *g, LukeRxId id, const char *name) {
  LukeRxNode *n = luke_rx_node_raw(g, id);
  if (!n || !name) return;
  size_t i = 0;
  for (; i + 1 < sizeof(n->name) && name[i]; ++i) n->name[i] = name[i];
  n->name[i] = 0;
}

static inline LukeRxId luke_rx_named(LukeRxGraph *g, LukeRxId id, const char *name) {
  luke_rx_set_name(g, id, name);
  return id;
}

static inline const char *luke_rx_name(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n) return "";
  return n->name[0] ? n->name : "";
}

static inline LukeRxId luke_rx_dep_at(LukeRxGraph *g, LukeRxId id, size_t i) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n || i >= n->dep_len) return 0;
  return n->deps[i];
}

static inline LukeRxId luke_rx_sub_at(LukeRxGraph *g, LukeRxId id, size_t i) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n || i >= n->sub_len) return 0;
  return n->subs[i];
}

static inline const char *luke_rx_kind_name(LukeRxKind k) {
  switch (k) {
    case LUKE_RX_CELL: return "cell";
    case LUKE_RX_DERIVED: return "derived";
    case LUKE_RX_EFFECT: return "effect";
    case LUKE_RX_LIST: return "list";
    case LUKE_RX_MAP: return "map";
    default: return "node";
  }
}

/* Stable buffer for gdb / DAP evaluate — not re-entrant. */
static char luke_rx_inspect_buf[16384];

static inline int luke_rx_inspect_put(char *buf, size_t buflen, size_t *o, const char *s) {
  if (!s) return 0;
  while (*s) {
    if (*o + 1 >= buflen) return -1;
    buf[(*o)++] = *s++;
  }
  return 0;
}

static inline int luke_rx_inspect_putch(char *buf, size_t buflen, size_t *o, char c) {
  if (*o + 1 >= buflen) return -1;
  buf[(*o)++] = c;
  return 0;
}

static inline int luke_rx_inspect_put_esc(char *buf, size_t buflen, size_t *o, const char *s) {
  if (!s) return 0;
  for (; *s; ++s) {
    if (*s == '"' || *s == '\\') {
      if (luke_rx_inspect_putch(buf, buflen, o, '\\') || luke_rx_inspect_putch(buf, buflen, o, *s))
        return -1;
    } else if ((unsigned char)*s < 0x20) {
      if (luke_rx_inspect_putch(buf, buflen, o, '?')) return -1;
    } else if (luke_rx_inspect_putch(buf, buflen, o, *s))
      return -1;
  }
  return 0;
}

static inline int luke_rx_inspect_put_u(char *buf, size_t buflen, size_t *o, unsigned v) {
  char tmp[16];
  int n = snprintf(tmp, sizeof(tmp), "%u", v);
  if (n < 0) return -1;
  return luke_rx_inspect_put(buf, buflen, o, tmp);
}

static inline int luke_rx_inspect_json(LukeRxGraph *g, char *buf, size_t buflen) {
  size_t o = 0;
  int first = 1;
  if (!buf || buflen < 3) return -1;
  if (luke_rx_inspect_put(buf, buflen, &o, "{\"cells\":[")) return -1;
  if (g) {
    LukeRxId id;
    for (id = 1; id < (LukeRxId)g->len; ++id) {
      LukeRxNode *n = luke_rx_node(g, id);
      char vbuf[128];
      size_t i;
      if (!n) continue;
      if (!first && luke_rx_inspect_putch(buf, buflen, &o, ',')) return -1;
      first = 0;
      if (luke_rx_inspect_put(buf, buflen, &o, "{\"id\":") ||
          luke_rx_inspect_put_u(buf, buflen, &o, (unsigned)id) ||
          luke_rx_inspect_put(buf, buflen, &o, ",\"name\":\""))
        return -1;
      if (luke_rx_inspect_put_esc(buf, buflen, &o, n->name[0] ? n->name : "")) return -1;
      if (luke_rx_inspect_put(buf, buflen, &o, "\",\"kind\":\"") ||
          luke_rx_inspect_put(buf, buflen, &o, luke_rx_kind_name(n->kind)) ||
          luke_rx_inspect_put(buf, buflen, &o, "\",\"value\":\""))
        return -1;
      if (n->vkind == LUKE_RX_VAL_TEXT) {
        size_t lim = n->text.len < 64 ? n->text.len : 64;
        for (i = 0; i < lim; ++i) {
          char c = n->text.ptr ? n->text.ptr[i] : 0;
          if (c == '"' || c == '\\') {
            if (luke_rx_inspect_putch(buf, buflen, &o, '\\') ||
                luke_rx_inspect_putch(buf, buflen, &o, c))
              return -1;
          } else if ((unsigned char)c < 0x20) {
            if (luke_rx_inspect_putch(buf, buflen, &o, '?')) return -1;
          } else if (luke_rx_inspect_putch(buf, buflen, &o, c))
            return -1;
        }
      } else if (n->vkind == LUKE_RX_VAL_INT) {
        snprintf(vbuf, sizeof(vbuf), "%lld", (long long)n->i64);
        if (luke_rx_inspect_put_esc(buf, buflen, &o, vbuf)) return -1;
      } else if (n->vkind == LUKE_RX_VAL_LIST) {
        snprintf(vbuf, sizeof(vbuf), "list(len=%g)", luke_list_len(n->list));
        if (luke_rx_inspect_put_esc(buf, buflen, &o, vbuf)) return -1;
      } else if (n->vkind == LUKE_RX_VAL_MAP) {
        snprintf(vbuf, sizeof(vbuf), "map(len=%g)", luke_map_len(n->map));
        if (luke_rx_inspect_put_esc(buf, buflen, &o, vbuf)) return -1;
      } else {
        snprintf(vbuf, sizeof(vbuf), "%.10g", n->num);
        if (luke_rx_inspect_put_esc(buf, buflen, &o, vbuf)) return -1;
      }
      if (luke_rx_inspect_put(buf, buflen, &o, "\",\"deps\":[")) return -1;
      for (i = 0; i < n->dep_len; ++i) {
        LukeRxId d = n->deps[i];
        LukeRxNode *dn = luke_rx_node(g, d);
        if (i && luke_rx_inspect_putch(buf, buflen, &o, ',')) return -1;
        if (luke_rx_inspect_put(buf, buflen, &o, "{\"id\":") ||
            luke_rx_inspect_put_u(buf, buflen, &o, (unsigned)d) ||
            luke_rx_inspect_put(buf, buflen, &o, ",\"name\":\""))
          return -1;
        if (luke_rx_inspect_put_esc(buf, buflen, &o, dn && dn->name[0] ? dn->name : "")) return -1;
        if (luke_rx_inspect_put(buf, buflen, &o, "\"}")) return -1;
      }
      if (luke_rx_inspect_put(buf, buflen, &o, "],\"subs\":[")) return -1;
      for (i = 0; i < n->sub_len; ++i) {
        LukeRxId s = n->subs[i];
        LukeRxNode *sn = luke_rx_node(g, s);
        if (i && luke_rx_inspect_putch(buf, buflen, &o, ',')) return -1;
        if (luke_rx_inspect_put(buf, buflen, &o, "{\"id\":") ||
            luke_rx_inspect_put_u(buf, buflen, &o, (unsigned)s) ||
            luke_rx_inspect_put(buf, buflen, &o, ",\"name\":\""))
          return -1;
        if (luke_rx_inspect_put_esc(buf, buflen, &o, sn && sn->name[0] ? sn->name : "")) return -1;
        if (luke_rx_inspect_put(buf, buflen, &o, "\"}")) return -1;
      }
      if (luke_rx_inspect_put(buf, buflen, &o, "]}")) return -1;
    }
  }
  if (luke_rx_inspect_put(buf, buflen, &o, "]}")) return -1;
  buf[o] = 0;
  return (int)o;
}

/* Out-of-line so gdb/DAP can resolve the symbol (static inline is often omitted). */
__attribute__((used, noinline)) static const char *luke_rx_inspect_cstr(LukeRxGraph *g) {
  if (luke_rx_inspect_json(g, luke_rx_inspect_buf, sizeof(luke_rx_inspect_buf)) < 0) {
    luke_rx_inspect_buf[0] = '{';
    luke_rx_inspect_buf[1] = '}';
    luke_rx_inspect_buf[2] = 0;
  }
  return luke_rx_inspect_buf;
}

__attribute__((used, noinline)) static void luke_rx_inspect_print(LukeRxGraph *g) {
  fprintf(stderr, "%s\n", luke_rx_inspect_cstr(g));
}

static inline int luke_rx_trace_why(LukeRxGraph *g, LukeRxId id) {
  if (!g) return 0;
  g->why_trace_count++;
  LukeRxId root = luke_rx_why_root(g, id);
  fprintf(stderr, "why#%u root=%u depth=%d last_write=%u deps=%zu subs=%zu\n",
          (unsigned)id, (unsigned)root, luke_rx_why_depth(g, id),
          (unsigned)g->last_write_id, luke_rx_dep_count(g, id), luke_rx_sub_count(g, id));
  return g->why_trace_count;
}

#ifdef __cplusplus
}
#endif

#endif /* LUKE_REACTIVE_H */
