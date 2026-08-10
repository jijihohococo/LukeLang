#ifndef ARGUS_H
#define ARGUS_H

/* Argus — LukeLang rendering engine.
 * DOM presentment (not Skia). Explicit frames until the layout engine lands.
 * See docs/ARGUS.md */

#include "luke_rt.h"

#if !defined(LUKE_BROWSER) && !defined(_WIN32)
#include <time.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArgusKind {
  ARGUS_BOX = 0,
  ARGUS_TEXT = 1,
  ARGUS_BUTTON = 2,
  ARGUS_IMAGE = 3,
  ARGUS_INPUT = 4,
  ARGUS_SELECT = 5,
  ARGUS_TABLE = 6,
  ARGUS_MODAL = 7
} ArgusKind;

typedef struct ArgusNode {
  char id[64];
  ArgusKind kind;
  double x, y, w, h;
  double opacity;
  LukeText text;
  LukeText src;
  LukeText role;      /* a11y role override (empty = default) */
  LukeText aria_label; /* a11y label */
  int input_type; /* 0 text, 1 password, 2 email, 3 checkbox, 4 radio */
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

__attribute__((import_module("lukejs"), import_name("argus_a11y"))) void
argus_js_a11y_raw(const char *id, size_t id_len, const char *role, size_t role_len,
                  const char *label, size_t label_len);

__attribute__((import_module("lukejs"), import_name("argus_select"))) void
argus_js_select_raw(const char *id, size_t id_len, const char *options, size_t options_len);

__attribute__((import_module("lukejs"), import_name("argus_table"))) void
argus_js_table_raw(const char *id, size_t id_len, const char *cells, size_t cells_len);

__attribute__((import_module("lukejs"), import_name("measure_text"))) double
luke_js_measure_text_raw(const char *text, size_t text_len);

__attribute__((import_module("lukejs"), import_name("viewport_width"))) double
luke_js_viewport_width_raw(void);

__attribute__((import_module("lukejs"), import_name("viewport_height"))) double
luke_js_viewport_height_raw(void);

__attribute__((import_module("lukejs"), import_name("now_ms"))) double
luke_js_now_ms_raw(void);

__attribute__((import_module("lukejs"), import_name("argus_fade"))) void
argus_js_fade_raw(const char *id, size_t id_len, double from, double to, double ms);

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
static inline void argus_js_a11y(LukeText id, LukeText role, LukeText label) {
  argus_js_a11y_raw(id.ptr, id.len, role.ptr, role.len, label.ptr, label.len);
}
static inline void argus_js_select(LukeText id, LukeText options) {
  argus_js_select_raw(id.ptr, id.len, options.ptr, options.len);
}
static inline void argus_js_table(LukeText id, LukeText cells) {
  argus_js_table_raw(id.ptr, id.len, cells.ptr, cells.len);
}
static inline double luke_js_measure_text(LukeText text) {
  return luke_js_measure_text_raw(text.ptr, text.len);
}
static inline double luke_js_viewport_width(void) { return luke_js_viewport_width_raw(); }
static inline double luke_js_viewport_height(void) { return luke_js_viewport_height_raw(); }
static inline double luke_js_now_ms(void) { return luke_js_now_ms_raw(); }
static inline void argus_js_fade(LukeText id, double from, double to, double ms) {
  argus_js_fade_raw(id.ptr, id.len, from, to, ms);
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
static inline void argus_js_a11y(LukeText id, LukeText role, LukeText label) {
  (void)id;
  (void)role;
  (void)label;
}
static inline void argus_js_select(LukeText id, LukeText options) {
  (void)id;
  (void)options;
}
static inline void argus_js_table(LukeText id, LukeText cells) {
  (void)id;
  (void)cells;
}
static inline double luke_js_measure_text(LukeText text) {
  /* Native beachhead: ~8px per character */
  return text.len ? (double)text.len * 8.0 : 0.0;
}
static inline double luke_js_viewport_width(void) { return 1280.0; }
static inline double luke_js_viewport_height(void) { return 720.0; }
static inline double luke_js_now_ms(void) {
#if defined(_WIN32)
  return 0.0;
#else
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}
static inline void argus_js_fade(LukeText id, double from, double to, double ms) {
  (void)id;
  (void)from;
  (void)to;
  (void)ms;
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

static inline void argus_set_a11y(ArgusNode *n, LukeText role, LukeText label) {
  if (!n) return;
  n->role = role;
  n->aria_label = label;
  n->dirty = 1;
}

static inline LukeText argus_default_role(const ArgusNode *n) {
  if (!n) return luke_text("");
  if (n->kind == ARGUS_BUTTON) return luke_text("button");
  if (n->kind == ARGUS_INPUT) {
    if (n->input_type == 3) return luke_text("checkbox");
    if (n->input_type == 4) return luke_text("radio");
    return luke_text("textbox");
  }
  if (n->kind == ARGUS_IMAGE) return luke_text("img");
  if (n->kind == ARGUS_SELECT) return luke_text("listbox");
  if (n->kind == ARGUS_TABLE) return luke_text("table");
  if (n->kind == ARGUS_MODAL) return luke_text("dialog");
  return luke_text("");
}

/* Paint a single node if dirty (Granularity — region paint). Returns 1 if painted. */
static inline int argus_paint_one(LukeArena *a, LukeText id) {
  ArgusTree *t = argus_tree(a);
  ArgusNode *n = argus_find(t, id);
  if (!n || (!n->dirty && n->mounted)) return 0;
  argus_js_upsert(id, (double)n->kind);
  argus_js_frame(id, n->x, n->y, n->w, n->h, n->opacity);
  LukeText role = n->role.len ? n->role : argus_default_role(n);
  if (role.len || n->aria_label.len) argus_js_a11y(id, role, n->aria_label);
  if (n->kind == ARGUS_IMAGE)
    argus_js_image(id, n->src);
  else if (n->kind == ARGUS_INPUT)
    argus_js_input(id, n->text, (double)n->input_type);
  else if (n->kind == ARGUS_SELECT)
    argus_js_select(id, n->text);
  else if (n->kind == ARGUS_TABLE)
    argus_js_table(id, n->text);
  else if (n->kind == ARGUS_TEXT || n->kind == ARGUS_BUTTON || n->kind == ARGUS_MODAL)
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

static inline int argus_place_select(LukeArena *a, LukeText id, double x, double y, double w,
                                     double h, LukeText options) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_SELECT);
  argus_set_frame(n, x, y, w, h);
  argus_set_text(n, options);
  return 1;
}

static inline int argus_place_table(LukeArena *a, LukeText id, double x, double y, double w,
                                    double h, LukeText cells) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_TABLE);
  argus_set_frame(n, x, y, w, h);
  argus_set_text(n, cells);
  return 1;
}

static inline int argus_place_modal(LukeArena *a, LukeText id, double x, double y, double w,
                                    double h, LukeText text) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_MODAL);
  argus_set_frame(n, x, y, w, h);
  argus_set_text(n, text);
  argus_set_a11y(n, luke_text("dialog"), text);
  return 1;
}

static inline double argus_measure_text(LukeText text) { return luke_js_measure_text(text); }

static inline double argus_viewport_width(void) { return luke_js_viewport_width(); }

static inline double argus_viewport_height(void) { return luke_js_viewport_height(); }

static inline double argus_now_ms(void) { return luke_js_now_ms(); }

static inline int argus_set_opacity_id(LukeArena *a, LukeText id, double opacity) {
  ArgusNode *n = argus_find(argus_tree(a), id);
  if (!n) return 0;
  if (opacity < 0) opacity = 0;
  if (opacity > 1) opacity = 1;
  argus_set_opacity(n, opacity);
  return 1;
}

static inline double argus_ease_out_cubic(double t) {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  double u = 1.0 - t;
  return 1.0 - u * u * u;
}

/* Browser: rAF + ease-out via lukejs. Native: stepped sync paint. */
static inline int argus_fade_to(LukeArena *a, LukeText id, double from, double to, double ms) {
  ArgusNode *n = argus_find(argus_tree(a), id);
  if (from < 0 && n) from = n->opacity;
  if (from < 0) from = 0;
  if (to < 0) to = 0;
  if (to > 1) to = 1;
#if defined(LUKE_BROWSER)
  argus_js_fade(id, from, to, ms);
  if (n) argus_set_opacity(n, to);
  return n ? 1 : 0;
#else
  if (!n) return 0;
  int steps = (int)(ms / 16.0);
  if (steps < 1) steps = 1;
  if (steps > 48) steps = 48;
  for (int i = 0; i <= steps; ++i) {
    double t = (double)i / (double)steps;
    argus_set_opacity(n, from + (to - from) * argus_ease_out_cubic(t));
    argus_paint(a);
  }
  return 1;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* ARGUS_H */
