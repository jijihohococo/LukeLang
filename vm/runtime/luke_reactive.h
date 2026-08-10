#ifndef LUKE_REACTIVE_H
#define LUKE_REACTIVE_H

/* Luke Reactive Runtime — Phase 1 core + Phase 2 UI flush hooks
 * See docs/REACTIVE.md
 *
 * Doctrine: Lukelang understands change.
 * Cells / derived / effects + batched scheduler.
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
  LUKE_RX_EFFECT = 3
} LukeRxKind;

typedef enum LukeRxWave {
  LUKE_RX_WAVE_INVALIDATE = 1,
  LUKE_RX_WAVE_RECOMPUTE = 2,
  LUKE_RX_WAVE_EFFECT = 3,
  LUKE_RX_WAVE_LAYOUT = 4,
  LUKE_RX_WAVE_PAINT = 5
} LukeRxWave;

typedef struct LukeRxGraph LukeRxGraph;

typedef double (*LukeRxComputeFn)(LukeRxGraph *g, void *ctx);
typedef void (*LukeRxEffectFn)(LukeRxGraph *g, void *ctx);
typedef void (*LukeRxAfterFlushFn)(LukeRxGraph *g);

typedef enum LukeRxValKind {
  LUKE_RX_VAL_NUM = 0,
  LUKE_RX_VAL_TEXT = 1
} LukeRxValKind;

typedef struct LukeRxNode {
  LukeRxId id;
  LukeRxKind kind;
  LukeRxValKind vkind;
  int dirty;
  int version;
  double num;
  LukeText text;
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
};

static inline void luke_rx_graph_init(LukeRxGraph *g, LukeArena *a) {
  if (!g) return;
  memset(g, 0, sizeof(*g));
  g->arena = a;
}

static inline LukeRxNode *luke_rx_node(LukeRxGraph *g, LukeRxId id) {
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
  return id;
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
  if (n->dirty) return;
  n->dirty = 1;
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

static inline LukeRxId luke_rx_derived(LukeRxGraph *g, LukeRxComputeFn fn, void *ctx) {
  LukeRxId id = luke_rx_alloc(g, LUKE_RX_DERIVED);
  LukeRxNode *n = luke_rx_node(g, id);
  if (n) {
    n->vkind = LUKE_RX_VAL_NUM;
    n->compute = fn;
    n->ctx = ctx;
    luke_rx_mark_dirty(g, id);
  }
  return id;
}

static inline LukeRxId luke_rx_effect(LukeRxGraph *g, LukeRxEffectFn fn, void *ctx) {
  LukeRxId id = luke_rx_alloc(g, LUKE_RX_EFFECT);
  LukeRxNode *n = luke_rx_node(g, id);
  if (n) {
    n->effect = fn;
    n->ctx = ctx;
    luke_rx_mark_dirty(g, id);
  }
  return id;
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

static inline int luke_rx_flush(LukeRxGraph *g) {
  if (!g || g->dirty_len == 0) return 0;
  g->cycle_tripped = 0;

  /* Wave 1 — invalidate already queued; ensure transitive dirty marks. */
  for (size_t i = 0; i < g->dirty_len; ++i)
    luke_rx_invalidate_subs(g, g->dirty_q[i]);

  /* Wave 2 — recompute pure derived (iterate; prefer nodes whose deps are clean). */
  int guard = (int)(g->len * 4 + 8);
  while (guard-- > 0) {
    int progressed = 0;
    int pending_derived = 0;
    for (LukeRxId id = 1; id < (LukeRxId)g->len; ++id) {
      LukeRxNode *n = &g->nodes[id];
      if (!n->dirty || n->kind != LUKE_RX_DERIVED) continue;
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
        progressed = 1;
        continue;
      }
      /* Drop prior dynamic edges — re-register on read during compute. */
      n->dep_len = 0;
      g->computing = id;
      g->stale_read = 0;
      double v = n->compute(g, n->ctx);
      g->computing = 0;
      if (g->stale_read) {
        /* Deps not ready (or cycle) — keep dirty; try again later this wave. */
        g->stale_read = 0;
        continue;
      }
      n->num = v;
      n->version++;
      n->dirty = 0;
      progressed = 1;
    }
    if (!pending_derived) break;
    if (!progressed) {
      g->cycle_tripped = 1;
      fprintf(stderr, "Luke Reactive: cycle detected in derived graph (epoch %d)\n", g->epoch);
      /* Force-clear to avoid infinite loops at the call site. */
      for (LukeRxId id = 1; id < (LukeRxId)g->len; ++id) {
        if (g->nodes[id].kind == LUKE_RX_DERIVED) g->nodes[id].dirty = 0;
      }
      break;
    }
  }

  /* Wave 3 — effects (after pure recompute). */
  for (LukeRxId id = 1; id < (LukeRxId)g->len; ++id) {
    LukeRxNode *n = &g->nodes[id];
    if (!n->dirty || n->kind != LUKE_RX_EFFECT) continue;
    n->dep_len = 0;
    g->computing = id;
    if (n->effect) n->effect(g, n->ctx);
    g->computing = 0;
    n->version++;
    n->dirty = 0;
  }

  /* Clear remaining cell dirty flags (cells hold values already). */
  for (LukeRxId id = 1; id < (LukeRxId)g->len; ++id) {
    if (g->nodes[id].kind == LUKE_RX_CELL) g->nodes[id].dirty = 0;
  }
  g->dirty_len = 0;
  g->epoch++;
  /* Waves 4–5 — layout/paint consumers (Hanka / Argus). Text-only UI binds
   * set need_paint; size/structure changes may set need_layout. */
  if (g->after_flush) g->after_flush(g);
  return g->cycle_tripped ? -1 : 1;
}

static inline void luke_rx_batch_begin(LukeRxGraph *g) {
  if (g) g->batching++;
}

static inline void luke_rx_batch_end(LukeRxGraph *g) {
  if (!g) return;
  if (g->batching > 0) g->batching--;
  if (g->batching == 0) luke_rx_flush(g);
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
  if (!g->batching) luke_rx_flush(g);
}

static inline int luke_rx_text_eq(LukeText a, LukeText b) {
  return a.len == b.len && (a.len == 0 || (a.ptr && b.ptr && memcmp(a.ptr, b.ptr, a.len) == 0));
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
  if (!g->batching) luke_rx_flush(g);
}

static inline double luke_rx_read_num(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n) return 0.0;
  if (g->computing) {
    luke_rx_link(g, g->computing, id);
    if (n->dirty && n->kind == LUKE_RX_DERIVED) g->stale_read = 1;
  }
  if (n->dirty && n->kind == LUKE_RX_DERIVED && !g->computing) luke_rx_flush(g);
  n = luke_rx_node(g, id);
  return n ? n->num : 0.0;
}

static inline LukeText luke_rx_read_text(LukeRxGraph *g, LukeRxId id) {
  LukeRxNode *n = luke_rx_node(g, id);
  if (!n) return luke_text("");
  if (g->computing) {
    luke_rx_link(g, g->computing, id);
    if (n->dirty && n->kind == LUKE_RX_DERIVED) g->stale_read = 1;
  }
  if (n->dirty && n->kind == LUKE_RX_DERIVED && !g->computing) luke_rx_flush(g);
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

#ifdef __cplusplus
}
#endif

#endif /* LUKE_REACTIVE_H */
