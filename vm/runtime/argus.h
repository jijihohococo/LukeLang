#ifndef ARGUS_H
#define ARGUS_H

/* Argus — LukeLang rendering engine.
 * DOM presentment (not Skia). Explicit frames until the layout engine lands.
 * See docs/ARGUS.md */

#include "luke_rt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArgusKind {
  ARGUS_BOX = 0,
  ARGUS_TEXT = 1,
  ARGUS_BUTTON = 2,
  ARGUS_IMAGE = 3,
  ARGUS_INPUT = 4
} ArgusKind;

typedef struct ArgusNode {
  char id[64];
  ArgusKind kind;
  double x, y, w, h;
  double opacity;
  LukeText text;
  LukeText src;
  int input_type; /* 0 text, 1 password, 2 email */
  int dirty;
  int mounted;
} ArgusNode;

typedef struct ArgusTree {
  LukeArena *arena;
  ArgusNode *nodes;
  size_t len;
  size_t cap;
  int painted;
} ArgusTree;

#if defined(LUKE_BROWSER)
__attribute__((import_module("lukejs"), import_name("argus_upsert"))) void
argus_js_upsert_raw(const char *id, size_t id_len, double kind);

__attribute__((import_module("lukejs"), import_name("argus_frame"))) void
argus_js_frame_raw(const char *id, size_t id_len, double x, double y, double w, double h,
                   double opacity);

__attribute__((import_module("lukejs"), import_name("argus_text"))) void
argus_js_text_raw(const char *id, size_t id_len, const char *text, size_t text_len);

__attribute__((import_module("lukejs"), import_name("argus_image"))) void
argus_js_image_raw(const char *id, size_t id_len, const char *src, size_t src_len);

__attribute__((import_module("lukejs"), import_name("argus_input"))) void
argus_js_input_raw(const char *id, size_t id_len, const char *placeholder, size_t placeholder_len,
                   double input_type);

__attribute__((import_module("lukejs"), import_name("argus_clear"))) void
argus_js_clear_raw(void);

static inline void argus_js_upsert(LukeText id, double kind) {
  argus_js_upsert_raw(id.ptr, id.len, kind);
}
static inline void argus_js_frame(LukeText id, double x, double y, double w, double h,
                                  double opacity) {
  argus_js_frame_raw(id.ptr, id.len, x, y, w, h, opacity);
}
static inline void argus_js_text(LukeText id, LukeText text) {
  argus_js_text_raw(id.ptr, id.len, text.ptr, text.len);
}
static inline void argus_js_image(LukeText id, LukeText src) {
  argus_js_image_raw(id.ptr, id.len, src.ptr, src.len);
}
static inline void argus_js_input(LukeText id, LukeText placeholder, double input_type) {
  argus_js_input_raw(id.ptr, id.len, placeholder.ptr, placeholder.len, input_type);
}
static inline void argus_js_clear(void) { argus_js_clear_raw(); }
#else
static inline void argus_js_upsert(LukeText id, double kind) {
  (void)id;
  (void)kind;
}
static inline void argus_js_frame(LukeText id, double x, double y, double w, double h,
                                  double opacity) {
  (void)id;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)opacity;
}
static inline void argus_js_text(LukeText id, LukeText text) {
  (void)id;
  (void)text;
}
static inline void argus_js_image(LukeText id, LukeText src) {
  (void)id;
  (void)src;
}
static inline void argus_js_input(LukeText id, LukeText placeholder, double input_type) {
  (void)id;
  (void)placeholder;
  (void)input_type;
}
static inline void argus_js_clear(void) {}
#endif

static ArgusTree argus_global = {0};

static inline void argus_id_copy(char *dst, size_t cap, LukeText id) {
  size_t n = id.len < cap - 1 ? id.len : cap - 1;
  if (n && id.ptr) memcpy(dst, id.ptr, n);
  dst[n] = '\0';
}

static inline ArgusTree *argus_tree(LukeArena *a) {
  if (!argus_global.arena) {
    argus_global.arena = a;
    argus_global.nodes = NULL;
    argus_global.len = 0;
    argus_global.cap = 0;
  }
  return &argus_global;
}

static inline ArgusNode *argus_find(ArgusTree *t, LukeText id) {
  for (size_t i = 0; i < t->len; ++i) {
    size_t n = strlen(t->nodes[i].id);
    if (n == id.len && (n == 0 || memcmp(t->nodes[i].id, id.ptr, n) == 0)) return &t->nodes[i];
  }
  return NULL;
}

static inline ArgusNode *argus_upsert(LukeArena *a, LukeText id, ArgusKind kind) {
  ArgusTree *t = argus_tree(a);
  ArgusNode *n = argus_find(t, id);
  if (n) {
    n->kind = kind;
    n->dirty = 1;
    return n;
  }
  if (t->len + 1 > t->cap) {
    size_t ncap = t->cap ? t->cap * 2 : 16;
    ArgusNode *nn = (ArgusNode *)luke_arena_alloc(a, ncap * sizeof(ArgusNode), sizeof(void *));
    if (t->nodes && t->len) memcpy(nn, t->nodes, t->len * sizeof(ArgusNode));
    t->nodes = nn;
    t->cap = ncap;
  }
  n = &t->nodes[t->len++];
  memset(n, 0, sizeof(*n));
  argus_id_copy(n->id, sizeof(n->id), id);
  n->kind = kind;
  n->opacity = 1.0;
  n->w = 100;
  n->h = 40;
  n->dirty = 1;
  return n;
}

static inline void argus_set_frame(ArgusNode *n, double x, double y, double w, double h) {
  if (!n) return;
  n->x = x;
  n->y = y;
  n->w = w;
  n->h = h;
  n->dirty = 1;
}

static inline void argus_set_text(ArgusNode *n, LukeText text) {
  if (!n) return;
  n->text = text;
  n->dirty = 1;
}

static inline void argus_set_src(ArgusNode *n, LukeText src) {
  if (!n) return;
  n->src = src;
  n->dirty = 1;
}

static inline void argus_set_opacity(ArgusNode *n, double opacity) {
  if (!n) return;
  n->opacity = opacity;
  n->dirty = 1;
}

/* Paint a single node if dirty (Granularity — region paint). Returns 1 if painted. */
static inline int argus_paint_one(LukeArena *a, LukeText id) {
  ArgusTree *t = argus_tree(a);
  ArgusNode *n = argus_find(t, id);
  if (!n || (!n->dirty && n->mounted)) return 0;
  argus_js_upsert(id, (double)n->kind);
  argus_js_frame(id, n->x, n->y, n->w, n->h, n->opacity);
  if (n->kind == ARGUS_IMAGE)
    argus_js_image(id, n->src);
  else if (n->kind == ARGUS_INPUT)
    argus_js_input(id, n->text, (double)n->input_type);
  else if (n->kind == ARGUS_TEXT || n->kind == ARGUS_BUTTON)
    argus_js_text(id, n->text);
  n->dirty = 0;
  n->mounted = 1;
  t->painted = 1;
  return 1;
}

static inline int argus_paint(LukeArena *a) {
  ArgusTree *t = argus_tree(a);
  int count = 0;
  for (size_t i = 0; i < t->len; ++i) {
    ArgusNode *n = &t->nodes[i];
    if (!n->dirty && n->mounted) continue;
    LukeText id = luke_text(n->id);
    if (argus_paint_one(a, id)) count++;
  }
  return count;
}

static inline void argus_clear(LukeArena *a) {
  ArgusTree *t = argus_tree(a);
  t->len = 0;
  t->painted = 0;
  argus_js_clear();
}

/* Convenience used by Build codegen / stdlib */
static inline int argus_place_text(LukeArena *a, LukeText id, double x, double y, double w,
                                   double h, LukeText text) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_TEXT);
  argus_set_frame(n, x, y, w, h);
  argus_set_text(n, text);
  return 1;
}

static inline int argus_place_button(LukeArena *a, LukeText id, double x, double y, double w,
                                     double h, LukeText text) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_BUTTON);
  argus_set_frame(n, x, y, w, h);
  argus_set_text(n, text);
  return 1;
}

static inline int argus_place_image(LukeArena *a, LukeText id, double x, double y, double w,
                                    double h, LukeText src) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_IMAGE);
  argus_set_frame(n, x, y, w, h);
  argus_set_src(n, src);
  return 1;
}

static inline int argus_place_box(LukeArena *a, LukeText id, double x, double y, double w,
                                  double h) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_BOX);
  argus_set_frame(n, x, y, w, h);
  return 1;
}

static inline int argus_place_input(LukeArena *a, LukeText id, double x, double y, double w,
                                    double h, LukeText placeholder, double input_type) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_INPUT);
  argus_set_frame(n, x, y, w, h);
  argus_set_text(n, placeholder);
  n->input_type = (int)input_type;
  return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* ARGUS_H */
