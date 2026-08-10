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
  LUKE_RX_VAL_MAP = 3
} LukeRxValKind;

typedef struct LukeRxNode {
  LukeRxId id;
  LukeRxKind kind;
  LukeRxValKind vkind;
  int dirty;
  int dead; /* disposed with component scope */
  int weak; /* effect: reads do not register dependency edges */
  int queued; /* enqueued on dirty_q this flush turn */
  uint8_t priority; /* effect scheduling lane (Scheduler 2.0) */
  uint8_t wait_epochs; /* starvation guard — epochs waiting to run */
  int version;
  uint32_t scope_id; /* owning scope frame (0 = global) */
  double num;
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
    if (n) n->scope_id = s->scope_id;
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
      if (n->dead || !n->dirty || n->kind != LUKE_RX_DERIVED) continue;
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
    if (n->dead || !n->dirty || n->kind != LUKE_RX_EFFECT) continue;
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
    if (n->dead || !n->dirty || n->kind != LUKE_RX_EFFECT) continue;
    if (n->priority == LUKE_RX_PRIO_BACKGROUND && saw_ui) g->ui_before_bg = 1;
    if (n->priority == LUKE_RX_PRIO_UI) saw_ui = 1;
    if (!g->last_first_effect) g->last_first_effect = id;
    g->last_last_effect = id;
    luke_rx_clear_deps(g, id);
    g->computing = id;
    if (n->effect) n->effect(g, n->ctx);
    g->computing = 0;
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
  if (n->vkind == LUKE_RX_VAL_TEXT) return;
  if (n->num == v && !n->dirty) return;
  n->num = v;
  n->version++;
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
  return n ? n->num : 0.0;
}

/* Weak read — observe value without registering a dependency edge. */
static inline double luke_rx_read_num_weak(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n) return 0.0;
  if (g->computing) g->weak_read_count++;
  if (n->dirty && n->kind == LUKE_RX_DERIVED && !g->computing) luke_rx_request_flush(g);
  n = luke_rx_node(g, id);
  return n ? n->num : 0.0;
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

#ifdef __cplusplus
}
#endif

#endif /* LUKE_REACTIVE_H */
