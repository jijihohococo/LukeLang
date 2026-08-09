#ifndef LUKE_RENDER_H
#define LUKE_RENDER_H

/* Luke Rendering Engine v0 — DOM presentment, not Skia.
 * Layout is explicit frames for now (see docs/LAYOUT_ENGINE.md). */

#include "luke_rt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum LukeRenderKind {
  LUKE_RENDER_BOX = 0,
  LUKE_RENDER_TEXT = 1,
  LUKE_RENDER_BUTTON = 2,
  LUKE_RENDER_IMAGE = 3
} LukeRenderKind;

typedef struct LukeRenderNode {
  char id[64];
  LukeRenderKind kind;
  double x, y, w, h;
  double opacity;
  LukeText text;
  LukeText src;
  int dirty;
  int mounted;
} LukeRenderNode;

typedef struct LukeRenderTree {
  LukeArena *arena;
  LukeRenderNode *nodes;
  size_t len;
  size_t cap;
  int painted;
} LukeRenderTree;

#if defined(LUKE_BROWSER)
__attribute__((import_module("lukejs"), import_name("render_upsert"))) void
luke_js_render_upsert_raw(const char *id, size_t id_len, double kind);

__attribute__((import_module("lukejs"), import_name("render_frame"))) void
luke_js_render_frame_raw(const char *id, size_t id_len, double x, double y, double w, double h,
                          double opacity);

__attribute__((import_module("lukejs"), import_name("render_text"))) void
luke_js_render_text_raw(const char *id, size_t id_len, const char *text, size_t text_len);

__attribute__((import_module("lukejs"), import_name("render_image"))) void
luke_js_render_image_raw(const char *id, size_t id_len, const char *src, size_t src_len);

__attribute__((import_module("lukejs"), import_name("render_clear"))) void
luke_js_render_clear_raw(void);

static inline void luke_js_render_upsert(LukeText id, double kind) {
  luke_js_render_upsert_raw(id.ptr, id.len, kind);
}
static inline void luke_js_render_frame(LukeText id, double x, double y, double w, double h,
                                        double opacity) {
  luke_js_render_frame_raw(id.ptr, id.len, x, y, w, h, opacity);
}
static inline void luke_js_render_text(LukeText id, LukeText text) {
  luke_js_render_text_raw(id.ptr, id.len, text.ptr, text.len);
}
static inline void luke_js_render_image(LukeText id, LukeText src) {
  luke_js_render_image_raw(id.ptr, id.len, src.ptr, src.len);
}
static inline void luke_js_render_clear(void) { luke_js_render_clear_raw(); }
#else
static inline void luke_js_render_upsert(LukeText id, double kind) {
  (void)id;
  (void)kind;
}
static inline void luke_js_render_frame(LukeText id, double x, double y, double w, double h,
                                        double opacity) {
  (void)id;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)opacity;
}
static inline void luke_js_render_text(LukeText id, LukeText text) {
  (void)id;
  (void)text;
}
static inline void luke_js_render_image(LukeText id, LukeText src) {
  (void)id;
  (void)src;
}
static inline void luke_js_render_clear(void) {}
#endif

static LukeRenderTree luke_render_global = {0};

static inline void luke_render_id_copy(char *dst, size_t cap, LukeText id) {
  size_t n = id.len < cap - 1 ? id.len : cap - 1;
  if (n && id.ptr) memcpy(dst, id.ptr, n);
  dst[n] = '\0';
}

static inline LukeRenderTree *luke_render_tree(LukeArena *a) {
  if (!luke_render_global.arena) {
    luke_render_global.arena = a;
    luke_render_global.nodes = NULL;
    luke_render_global.len = 0;
    luke_render_global.cap = 0;
  }
  return &luke_render_global;
}

static inline LukeRenderNode *luke_render_find(LukeRenderTree *t, LukeText id) {
  for (size_t i = 0; i < t->len; ++i) {
    size_t n = strlen(t->nodes[i].id);
    if (n == id.len && (n == 0 || memcmp(t->nodes[i].id, id.ptr, n) == 0)) return &t->nodes[i];
  }
  return NULL;
}

static inline LukeRenderNode *luke_render_upsert(LukeArena *a, LukeText id, LukeRenderKind kind) {
  LukeRenderTree *t = luke_render_tree(a);
  LukeRenderNode *n = luke_render_find(t, id);
  if (n) {
    n->kind = kind;
    n->dirty = 1;
    return n;
  }
  if (t->len + 1 > t->cap) {
    size_t ncap = t->cap ? t->cap * 2 : 16;
    LukeRenderNode *nn =
        (LukeRenderNode *)luke_arena_alloc(a, ncap * sizeof(LukeRenderNode), sizeof(void *));
    if (t->nodes && t->len) memcpy(nn, t->nodes, t->len * sizeof(LukeRenderNode));
    t->nodes = nn;
    t->cap = ncap;
  }
  n = &t->nodes[t->len++];
  memset(n, 0, sizeof(*n));
  luke_render_id_copy(n->id, sizeof(n->id), id);
  n->kind = kind;
  n->opacity = 1.0;
  n->w = 100;
  n->h = 40;
  n->dirty = 1;
  return n;
}

static inline void luke_render_set_frame(LukeRenderNode *n, double x, double y, double w, double h) {
  if (!n) return;
  n->x = x;
  n->y = y;
  n->w = w;
  n->h = h;
  n->dirty = 1;
}

static inline void luke_render_set_text(LukeRenderNode *n, LukeText text) {
  if (!n) return;
  n->text = text;
  n->dirty = 1;
}

static inline void luke_render_set_src(LukeRenderNode *n, LukeText src) {
  if (!n) return;
  n->src = src;
  n->dirty = 1;
}

static inline void luke_render_paint(LukeArena *a) {
  LukeRenderTree *t = luke_render_tree(a);
  for (size_t i = 0; i < t->len; ++i) {
    LukeRenderNode *n = &t->nodes[i];
    if (!n->dirty && n->mounted) continue;
    LukeText id = luke_text(n->id);
    luke_js_render_upsert(id, (double)n->kind);
    luke_js_render_frame(id, n->x, n->y, n->w, n->h, n->opacity);
    if (n->kind == LUKE_RENDER_IMAGE)
      luke_js_render_image(id, n->src);
    else if (n->kind == LUKE_RENDER_TEXT || n->kind == LUKE_RENDER_BUTTON)
      luke_js_render_text(id, n->text);
    n->dirty = 0;
    n->mounted = 1;
  }
  t->painted = 1;
}

/* Convenience used by Build codegen / stdlib */
static inline int luke_render_place_text(LukeArena *a, LukeText id, double x, double y, double w,
                                        double h, LukeText text) {
  LukeRenderNode *n = luke_render_upsert(a, id, LUKE_RENDER_TEXT);
  luke_render_set_frame(n, x, y, w, h);
  luke_render_set_text(n, text);
  return 1;
}

static inline int luke_render_place_button(LukeArena *a, LukeText id, double x, double y, double w,
                                          double h, LukeText text) {
  LukeRenderNode *n = luke_render_upsert(a, id, LUKE_RENDER_BUTTON);
  luke_render_set_frame(n, x, y, w, h);
  luke_render_set_text(n, text);
  return 1;
}

static inline int luke_render_place_image(LukeArena *a, LukeText id, double x, double y, double w,
                                         double h, LukeText src) {
  LukeRenderNode *n = luke_render_upsert(a, id, LUKE_RENDER_IMAGE);
  luke_render_set_frame(n, x, y, w, h);
  luke_render_set_src(n, src);
  return 1;
}

static inline int luke_render_place_box(LukeArena *a, LukeText id, double x, double y, double w,
                                       double h) {
  LukeRenderNode *n = luke_render_upsert(a, id, LUKE_RENDER_BOX);
  luke_render_set_frame(n, x, y, w, h);
  return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* LUKE_RENDER_H */
