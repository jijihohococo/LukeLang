#ifndef ARGUS_H
#define ARGUS_H

/* Argus — LukeLang rendering engine.
 * DOM presentment (not Skia). Path A: reactive patcher over CSS flex flow;
 * absolute frames remain for STACK / explicit PLACE. See docs/ARGUS.md
 *
 * Scene nodes + id index are malloc-backed (stable across bump-arena growth).
 * Ids are owned copies freed on CLEAR — not pointers into the bump arena. */

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
  LukeText id;        /* malloc-owned; freed on CLEAR */
  LukeText parent_id; /* malloc-owned or empty; freed on CLEAR */
  ArgusKind kind;
  double x, y, w, h;
  double opacity;
  LukeText text;
  LukeText src;
  LukeText role;       /* a11y role override (empty = default) */
  LukeText aria_label; /* a11y label */
  int input_type; /* 0 text, 1 password, 2 email, 3 checkbox, 4 radio */
  /* Path A — CSS flow / flex (0 = classic absolute frame) */
  int flow;      /* 1 = normal-flow child (no left/top) */
  int flex_dir;  /* 0 none, 1 column, 2 row */
  double flex_gap;
  double flex_pad;
  int flex_align; /* main-axis 0 start, 1 center, 2 end */
  int flex_cross; /* cross-axis 0 start, 1 center, 2 end */
  int flex_wrap;
  int flex_grow; /* Path A: 1 = flex-grow fill (AUTO width/height on flow) */
  int scroll;    /* 1 = overflow:auto */
  int grid_cols; /* >0 → CSS grid with N columns (flex_dir unused) */
  int live;      /* 0 none, 1 polite, 2 assertive */
  LukeText css_class; /* WEAR class hatch */
  int dirty;
  int mounted;
} ArgusNode;

/* Open-addressed id → node-index map (O(1) find). Nodes live in malloc, not the bump
 * arena — arena realloc must not invalidate the scene table. */
typedef struct ArgusIdSlot {
  uint32_t hash;
  uint32_t index; /* 1-based into nodes[]; 0 = empty */
} ArgusIdSlot;

typedef struct ArgusTree {
  LukeArena *arena;
  ArgusNode *nodes; /* malloc/realloc */
  size_t len;
  size_t cap;
  ArgusIdSlot *slots; /* malloc/realloc, power-of-two */
  size_t slot_cap;
  size_t slot_used;
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

__attribute__((import_module("lukejs"), import_name("argus_parent"))) void
argus_js_parent_raw(const char *id, size_t id_len, const char *parent, size_t parent_len);

__attribute__((import_module("lukejs"), import_name("argus_flex"))) void
argus_js_flex_raw(const char *id, size_t id_len, double dir, double gap, double pad, double align,
                  double cross, double wrap);

__attribute__((import_module("lukejs"), import_name("argus_flow_frame"))) void
argus_js_flow_frame_raw(const char *id, size_t id_len, double w, double h, double opacity,
                        double grow);

__attribute__((import_module("lukejs"), import_name("argus_focus_trap"))) void
argus_js_focus_trap_raw(const char *id, size_t id_len);

__attribute__((import_module("lukejs"), import_name("argus_focus_restore"))) void
argus_js_focus_restore_raw(void);

__attribute__((import_module("lukejs"), import_name("argus_announce"))) void
argus_js_announce_raw(const char *text, size_t text_len);

__attribute__((import_module("lukejs"), import_name("argus_modal_open"))) void
argus_js_modal_open_raw(const char *id, size_t id_len);

__attribute__((import_module("lukejs"), import_name("argus_modal_close"))) void
argus_js_modal_close_raw(const char *id, size_t id_len);

__attribute__((import_module("lukejs"), import_name("argus_live"))) void
argus_js_live_raw(const char *id, size_t id_len, double level);

__attribute__((import_module("lukejs"), import_name("argus_class"))) void
argus_js_class_raw(const char *id, size_t id_len, const char *cls, size_t cls_len);

__attribute__((import_module("lukejs"), import_name("argus_scroll"))) void
argus_js_scroll_raw(const char *id, size_t id_len, double on);

__attribute__((import_module("lukejs"), import_name("argus_grid"))) void
argus_js_grid_raw(const char *id, size_t id_len, double cols, double gap, double pad);

static inline void argus_js_upsert(LukeText id, double kind) {
  argus_js_upsert_raw(id.ptr, id.len, kind);
}
static inline void argus_js_frame(LukeText id, double x, double y, double w, double h,
                                  double opacity) {
  argus_js_frame_raw(id.ptr, id.len, x, y, w, h, opacity);
}
static inline void argus_js_parent(LukeText id, LukeText parent) {
  argus_js_parent_raw(id.ptr, id.len, parent.ptr, parent.len);
}
static inline void argus_js_flex(LukeText id, double dir, double gap, double pad, double align,
                                 double cross, double wrap) {
  argus_js_flex_raw(id.ptr, id.len, dir, gap, pad, align, cross, wrap);
}
static inline void argus_js_flow_frame(LukeText id, double w, double h, double opacity,
                                       double grow) {
  argus_js_flow_frame_raw(id.ptr, id.len, w, h, opacity, grow);
}
static inline void argus_js_focus_trap(LukeText id) { argus_js_focus_trap_raw(id.ptr, id.len); }
static inline void argus_js_focus_restore(void) { argus_js_focus_restore_raw(); }
static inline void argus_js_announce(LukeText text) {
  argus_js_announce_raw(text.ptr, text.len);
}
static inline void argus_js_modal_open(LukeText id) { argus_js_modal_open_raw(id.ptr, id.len); }
static inline void argus_js_modal_close(LukeText id) { argus_js_modal_close_raw(id.ptr, id.len); }
static inline void argus_js_live(LukeText id, double level) {
  argus_js_live_raw(id.ptr, id.len, level);
}
static inline void argus_js_class(LukeText id, LukeText cls) {
  argus_js_class_raw(id.ptr, id.len, cls.ptr, cls.len);
}
static inline void argus_js_scroll(LukeText id, double on) {
  argus_js_scroll_raw(id.ptr, id.len, on);
}
static inline void argus_js_grid(LukeText id, double cols, double gap, double pad) {
  argus_js_grid_raw(id.ptr, id.len, cols, gap, pad);
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
static inline void argus_js_parent(LukeText id, LukeText parent) {
  (void)id;
  (void)parent;
}
static inline void argus_js_flex(LukeText id, double dir, double gap, double pad, double align,
                                 double cross, double wrap) {
  (void)id;
  (void)dir;
  (void)gap;
  (void)pad;
  (void)align;
  (void)cross;
  (void)wrap;
}
static inline void argus_js_flow_frame(LukeText id, double w, double h, double opacity,
                                       double grow) {
  (void)id;
  (void)w;
  (void)h;
  (void)opacity;
  (void)grow;
}
static inline void argus_js_focus_trap(LukeText id) { (void)id; }
static inline void argus_js_focus_restore(void) {}
static inline void argus_js_announce(LukeText text) { (void)text; }
static inline void argus_js_modal_open(LukeText id) { (void)id; }
static inline void argus_js_modal_close(LukeText id) { (void)id; }
static inline void argus_js_live(LukeText id, double level) {
  (void)id;
  (void)level;
}
static inline void argus_js_class(LukeText id, LukeText cls) {
  (void)id;
  (void)cls;
}
static inline void argus_js_scroll(LukeText id, double on) {
  (void)id;
  (void)on;
}
static inline void argus_js_grid(LukeText id, double cols, double gap, double pad) {
  (void)id;
  (void)cols;
  (void)gap;
  (void)pad;
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

static inline LukeText argus_own_id(LukeText id) {
  char *p = (char *)malloc(id.len + 1);
  if (!p) {
    fprintf(stderr, "Argus: out of memory (id)\n");
    abort();
  }
  if (id.len && id.ptr) memcpy(p, id.ptr, id.len);
  p[id.len] = '\0';
  return luke_text_n(p, id.len);
}

static inline void argus_release_id(LukeText *t) {
  if (!t) return;
  if (t->ptr && t->len) free((void *)(uintptr_t)t->ptr);
  t->ptr = "";
  t->len = 0;
}

static inline uint32_t argus_hash_id(LukeText id) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < id.len; ++i) {
    h ^= (uint8_t)(id.ptr ? id.ptr[i] : 0);
    h *= 16777619u;
  }
  return h;
}

static inline void argus_index_clear(ArgusTree *t) {
  if (t->slots && t->slot_cap) memset(t->slots, 0, t->slot_cap * sizeof(ArgusIdSlot));
  t->slot_used = 0;
}

static inline void argus_index_insert(ArgusTree *t, LukeText id, size_t node_index) {
  uint32_t h = argus_hash_id(id);
  size_t mask = t->slot_cap - 1;
  size_t i = (size_t)h & mask;
  for (;;) {
    if (t->slots[i].index == 0) {
      t->slots[i].hash = h;
      t->slots[i].index = (uint32_t)(node_index + 1);
      t->slot_used++;
      return;
    }
    i = (i + 1) & mask;
  }
}

static inline void argus_index_grow(ArgusTree *t) {
  size_t ncap = t->slot_cap ? t->slot_cap * 2 : 64;
  ArgusIdSlot *old = t->slots;
  size_t old_cap = t->slot_cap;
  ArgusIdSlot *ns = (ArgusIdSlot *)calloc(ncap, sizeof(ArgusIdSlot));
  if (!ns) {
    fprintf(stderr, "Argus: out of memory (id index)\n");
    abort();
  }
  t->slots = ns;
  t->slot_cap = ncap;
  t->slot_used = 0;
  for (size_t i = 0; i < old_cap; ++i) {
    if (!old[i].index) continue;
    size_t ni = (size_t)old[i].index - 1;
    argus_index_insert(t, t->nodes[ni].id, ni);
  }
  free(old);
}

static inline ArgusTree *argus_tree(LukeArena *a) {
  if (!argus_global.arena) {
    argus_global.arena = a;
    argus_global.nodes = NULL;
    argus_global.len = 0;
    argus_global.cap = 0;
    argus_global.slots = NULL;
    argus_global.slot_cap = 0;
    argus_global.slot_used = 0;
  }
  return &argus_global;
}

static inline ArgusNode *argus_find(ArgusTree *t, LukeText id) {
  if (!t || !t->slots || !t->slot_cap || !t->len) return NULL;
  uint32_t h = argus_hash_id(id);
  size_t mask = t->slot_cap - 1;
  size_t i = (size_t)h & mask;
  for (size_t n = 0; n < t->slot_cap; ++n) {
    if (t->slots[i].index == 0) return NULL;
    if (t->slots[i].hash == h) {
      ArgusNode *node = &t->nodes[t->slots[i].index - 1];
      if (node->id.len == id.len &&
          (id.len == 0 || (id.ptr && node->id.ptr && memcmp(node->id.ptr, id.ptr, id.len) == 0)))
        return node;
    }
    i = (i + 1) & mask;
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
    ArgusNode *nn = (ArgusNode *)realloc(t->nodes, ncap * sizeof(ArgusNode));
    if (!nn) {
      fprintf(stderr, "Argus: out of memory (nodes)\n");
      abort();
    }
    t->nodes = nn;
    t->cap = ncap;
  }
  if (t->slot_cap == 0 || t->slot_used * 10 >= t->slot_cap * 7) argus_index_grow(t);
  size_t idx = t->len++;
  n = &t->nodes[idx];
  memset(n, 0, sizeof(*n));
  n->id = argus_own_id(id);
  n->parent_id = luke_text_n("", 0);
  n->kind = kind;
  n->opacity = 1.0;
  n->w = 100;
  n->h = 40;
  n->dirty = 1;
  argus_index_insert(t, n->id, idx);
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

static inline void argus_set_live(ArgusNode *n, int level) {
  if (!n) return;
  if (level < 0) level = 0;
  if (level > 2) level = 2;
  n->live = level;
  n->dirty = 1;
}

static inline void argus_set_class(ArgusNode *n, LukeText classes) {
  if (!n) return;
  n->css_class = classes;
  n->dirty = 1;
}

static inline void argus_set_scroll(ArgusNode *n, int on) {
  if (!n) return;
  n->scroll = on ? 1 : 0;
  n->dirty = 1;
}

static inline void argus_set_grid(ArgusNode *n, int cols, double gap, double pad) {
  if (!n) return;
  n->grid_cols = cols > 0 ? cols : 1;
  n->flex_dir = 0;
  n->flex_gap = gap;
  n->flex_pad = pad;
  n->dirty = 1;
}

static inline void argus_set_parent(ArgusNode *n, LukeText parent) {
  if (!n) return;
  argus_release_id(&n->parent_id);
  if (parent.len && parent.ptr)
    n->parent_id = argus_own_id(parent);
  else
    n->parent_id = luke_text_n("", 0);
  n->dirty = 1;
}

static inline void argus_set_flow(ArgusNode *n, int flow) {
  if (!n) return;
  n->flow = flow ? 1 : 0;
  n->dirty = 1;
}

static inline void argus_set_flex(ArgusNode *n, int dir, double gap, double pad, int align,
                                  int cross, int wrap) {
  if (!n) return;
  n->flex_dir = dir;
  n->flex_gap = gap;
  n->flex_pad = pad;
  n->flex_align = align;
  n->flex_cross = cross;
  n->flex_wrap = wrap ? 1 : 0;
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
  if (n->parent_id.len) argus_js_parent(id, n->parent_id);
  if (n->css_class.len) argus_js_class(id, n->css_class);
  if (n->grid_cols > 0)
    argus_js_grid(id, (double)n->grid_cols, n->flex_gap, n->flex_pad);
  else if (n->flex_dir)
    argus_js_flex(id, (double)n->flex_dir, n->flex_gap, n->flex_pad, (double)n->flex_align,
                  (double)n->flex_cross, (double)n->flex_wrap);
  if (n->scroll) argus_js_scroll(id, 1.0);
  if (n->flow)
    argus_js_flow_frame(id, n->w, n->h, n->opacity, (double)n->flex_grow);
  else
    argus_js_frame(id, n->x, n->y, n->w, n->h, n->opacity);
  LukeText role = n->role.len ? n->role : argus_default_role(n);
  if (role.len || n->aria_label.len) argus_js_a11y(id, role, n->aria_label);
  if (n->live) argus_js_live(id, (double)n->live);
  /* Modals mount closed — OPEN THE MODAL traps focus; paint must not. */
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
    if (argus_paint_one(a, n->id)) count++;
  }
  return count;
}

static inline void argus_clear(LukeArena *a) {
  ArgusTree *t = argus_tree(a);
  for (size_t i = 0; i < t->len; ++i) {
    if (t->nodes[i].kind == ARGUS_MODAL && t->nodes[i].mounted)
      argus_js_modal_close(t->nodes[i].id);
    argus_release_id(&t->nodes[i].id);
    argus_release_id(&t->nodes[i].parent_id);
  }
  t->len = 0;
  t->painted = 0;
  argus_index_clear(t);
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

/* Path A — flex container (dir: 1=column, 2=row). absolute=1 → positioned frame; else flow child. */
static inline int argus_place_flex(LukeArena *a, LukeText id, LukeText parent, int absolute,
                                   double x, double y, double w, double h, int dir, double gap,
                                   double pad, int align, int cross, int wrap) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_BOX);
  if (absolute) {
    argus_set_frame(n, x, y, w < 0 ? 0 : w, h < 0 ? 0 : h);
    argus_set_flow(n, 0);
    argus_set_parent(n, luke_text_n("", 0));
    n->flex_grow = 0;
  } else {
    int grow = (w < 0 || h < 0) ? 1 : 0;
    argus_set_frame(n, 0, 0, w < 0 ? 0 : w, h < 0 ? 0 : h);
    argus_set_flow(n, 1);
    argus_set_parent(n, parent);
    n->flex_grow = grow;
  }
  argus_set_flex(n, dir, gap, pad, align, cross, wrap);
  return 1;
}

/* Path A — CSS grid container (cols ≥ 1). */
static inline ArgusNode *argus_place_grid(LukeArena *a, LukeText id, LukeText parent, int absolute,
                                          double x, double y, double w, double h, int cols,
                                          double gap, double pad) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_BOX);
  if (absolute) {
    argus_set_frame(n, x, y, w < 0 ? 0 : w, h < 0 ? 0 : h);
    argus_set_flow(n, 0);
    argus_set_parent(n, luke_text_n("", 0));
    n->flex_grow = 0;
  } else {
    int grow = (w < 0 || h < 0) ? 1 : 0;
    argus_set_frame(n, 0, 0, w < 0 ? 0 : w, h < 0 ? 0 : h);
    argus_set_flow(n, 1);
    argus_set_parent(n, parent);
    n->flex_grow = grow;
  }
  argus_set_grid(n, cols, gap, pad);
  return n;
}

static inline int argus_place_flow_text(LukeArena *a, LukeText id, LukeText parent, double w,
                                        double h, LukeText text) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_TEXT);
  int grow = (w < 0 || h < 0) ? 1 : 0;
  argus_set_frame(n, 0, 0, w < 0 ? 0 : w, h < 0 ? 0 : h);
  argus_set_flow(n, 1);
  argus_set_parent(n, parent);
  n->flex_dir = 0;
  n->flex_grow = grow;
  argus_set_text(n, text);
  return 1;
}

static inline int argus_place_flow_button(LukeArena *a, LukeText id, LukeText parent, double w,
                                          double h, LukeText text) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_BUTTON);
  int grow = (w < 0 || h < 0) ? 1 : 0;
  argus_set_frame(n, 0, 0, w < 0 ? 0 : w, h < 0 ? 0 : h);
  argus_set_flow(n, 1);
  argus_set_parent(n, parent);
  n->flex_dir = 0;
  n->flex_grow = grow;
  argus_set_text(n, text);
  return 1;
}

static inline int argus_place_flow_image(LukeArena *a, LukeText id, LukeText parent, double w,
                                         double h, LukeText src) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_IMAGE);
  int grow = (w < 0 || h < 0) ? 1 : 0;
  argus_set_frame(n, 0, 0, w < 0 ? 0 : w, h < 0 ? 0 : h);
  argus_set_flow(n, 1);
  argus_set_parent(n, parent);
  n->flex_dir = 0;
  n->flex_grow = grow;
  argus_set_src(n, src);
  return 1;
}

static inline int argus_place_flow_box(LukeArena *a, LukeText id, LukeText parent, double w,
                                       double h) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_BOX);
  int grow = (w < 0 || h < 0) ? 1 : 0;
  argus_set_frame(n, 0, 0, w < 0 ? 0 : w, h < 0 ? 0 : h);
  argus_set_flow(n, 1);
  argus_set_parent(n, parent);
  n->flex_dir = 0;
  n->flex_grow = grow;
  return 1;
}

static inline int argus_place_flow_input(LukeArena *a, LukeText id, LukeText parent, double w,
                                         double h, LukeText placeholder, double input_type) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_INPUT);
  int grow = (w < 0 || h < 0) ? 1 : 0;
  argus_set_frame(n, 0, 0, w < 0 ? 0 : w, h < 0 ? 0 : h);
  argus_set_flow(n, 1);
  argus_set_parent(n, parent);
  n->flex_dir = 0;
  n->flex_grow = grow;
  argus_set_text(n, placeholder);
  n->input_type = (int)input_type;
  return 1;
}

static inline int argus_place_flow_select(LukeArena *a, LukeText id, LukeText parent, double w,
                                          double h, LukeText options) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_SELECT);
  argus_set_frame(n, 0, 0, w, h);
  argus_set_flow(n, 1);
  argus_set_parent(n, parent);
  n->flex_dir = 0;
  argus_set_text(n, options);
  return 1;
}

static inline int argus_place_flow_table(LukeArena *a, LukeText id, LukeText parent, double w,
                                         double h, LukeText cells) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_TABLE);
  argus_set_frame(n, 0, 0, w, h);
  argus_set_flow(n, 1);
  argus_set_parent(n, parent);
  n->flex_dir = 0;
  argus_set_text(n, cells);
  return 1;
}

static inline int argus_place_flow_modal(LukeArena *a, LukeText id, LukeText parent, double w,
                                         double h, LukeText text) {
  ArgusNode *n = argus_upsert(a, id, ARGUS_MODAL);
  argus_set_frame(n, 0, 0, w, h);
  argus_set_flow(n, 1);
  argus_set_parent(n, parent);
  n->flex_dir = 0;
  argus_set_text(n, text);
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

/* Show modal + trap focus. Pair with CLOSE / HIDE THE MODAL. */
static inline int argus_open_modal(LukeArena *a, LukeText id) {
  ArgusNode *n = argus_find(argus_tree(a), id);
  if (!n || n->kind != ARGUS_MODAL) {
    /* Still try JS — element may exist from paint. */
    argus_js_modal_open(id);
    return 0;
  }
  n->dirty = 1;
  argus_js_modal_open(id);
  return 1;
}

static inline int argus_close_modal(LukeArena *a, LukeText id) {
  (void)a;
  argus_js_modal_close(id);
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
