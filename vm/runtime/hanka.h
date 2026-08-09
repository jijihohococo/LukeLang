#ifndef HANKA_H
#define HANKA_H

/* Hanka — LukeLang layout engine.
 * Owns layout numbers (x,y,w,h). Browser flex/grid is not authority.
 * Emits frames into Argus for paint. See docs/HANKA.md */

#include "argus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum HankaAxis {
  HANKA_COLUMN = 0,
  HANKA_ROW = 1,
  HANKA_STACK = 2
} HankaAxis;

typedef enum HankaLeafKind {
  HANKA_LEAF_BOX = 0,
  HANKA_LEAF_TEXT = 1,
  HANKA_LEAF_BUTTON = 2,
  HANKA_LEAF_IMAGE = 3
} HankaLeafKind;

typedef struct HankaLeaf {
  char id[64];
  HankaLeafKind kind;
  double w, h;
  double ox, oy; /* STACK relative offset */
  int has_offset;
  LukeText text;
  LukeText src;
} HankaLeaf;

typedef struct HankaBox {
  HankaAxis axis;
  double x, y, w, h;
  double pad, gap;
  HankaLeaf *leaves;
  size_t len;
  size_t cap;
} HankaBox;

typedef struct HankaState {
  LukeArena *arena;
  HankaBox *boxes;
  size_t box_len;
  size_t box_cap;
  HankaBox *open; /* current BEGIN … box, or NULL */
  int open_axis;
} HankaState;

static HankaState hanka_global = {0};

static inline void hanka_id_copy(char *dst, size_t cap, LukeText id) {
  size_t n = id.len < cap - 1 ? id.len : cap - 1;
  if (n && id.ptr) memcpy(dst, id.ptr, n);
  dst[n] = '\0';
}

static inline HankaState *hanka_state(LukeArena *a) {
  if (!hanka_global.arena) {
    hanka_global.arena = a;
    hanka_global.boxes = NULL;
    hanka_global.box_len = 0;
    hanka_global.box_cap = 0;
    hanka_global.open = NULL;
  }
  return &hanka_global;
}

static inline void hanka_clear(LukeArena *a) {
  HankaState *s = hanka_state(a);
  s->box_len = 0;
  s->open = NULL;
}

static inline HankaBox *hanka_push_box(LukeArena *a, HankaAxis axis, double x, double y, double w,
                                       double h, double pad, double gap) {
  HankaState *s = hanka_state(a);
  if (s->open) {
    /* Auto-close previous open box so BEGIN is resilient. */
    s->open = NULL;
  }
  if (s->box_len + 1 > s->box_cap) {
    size_t ncap = s->box_cap ? s->box_cap * 2 : 8;
    HankaBox *nb = (HankaBox *)luke_arena_alloc(a, ncap * sizeof(HankaBox), sizeof(void *));
    if (s->boxes && s->box_len) memcpy(nb, s->boxes, s->box_len * sizeof(HankaBox));
    s->boxes = nb;
    s->box_cap = ncap;
  }
  HankaBox *b = &s->boxes[s->box_len++];
  memset(b, 0, sizeof(*b));
  b->axis = axis;
  b->x = x;
  b->y = y;
  b->w = w;
  b->h = h;
  b->pad = pad;
  b->gap = gap;
  s->open = b;
  s->open_axis = (int)axis;
  return b;
}

static inline int hanka_begin_column(LukeArena *a, double x, double y, double w, double h,
                                     double pad, double gap) {
  hanka_push_box(a, HANKA_COLUMN, x, y, w, h, pad, gap);
  return 1;
}

static inline int hanka_begin_row(LukeArena *a, double x, double y, double w, double h, double pad,
                                  double gap) {
  hanka_push_box(a, HANKA_ROW, x, y, w, h, pad, gap);
  return 1;
}

static inline int hanka_begin_stack(LukeArena *a, double x, double y, double w, double h, double pad,
                                    double gap) {
  hanka_push_box(a, HANKA_STACK, x, y, w, h, pad, gap);
  return 1;
}

static inline HankaLeaf *hanka_add_leaf(LukeArena *a, LukeText id, HankaLeafKind kind, double w,
                                        double h) {
  HankaState *s = hanka_state(a);
  if (!s->open) return NULL;
  HankaBox *b = s->open;
  if (b->len + 1 > b->cap) {
    size_t ncap = b->cap ? b->cap * 2 : 8;
    HankaLeaf *nl = (HankaLeaf *)luke_arena_alloc(a, ncap * sizeof(HankaLeaf), sizeof(void *));
    if (b->leaves && b->len) memcpy(nl, b->leaves, b->len * sizeof(HankaLeaf));
    b->leaves = nl;
    b->cap = ncap;
  }
  HankaLeaf *leaf = &b->leaves[b->len++];
  memset(leaf, 0, sizeof(*leaf));
  hanka_id_copy(leaf->id, sizeof(leaf->id), id);
  leaf->kind = kind;
  leaf->w = w;
  leaf->h = h;
  return leaf;
}

static inline int hanka_slot_text(LukeArena *a, LukeText id, double w, double h, LukeText text) {
  HankaLeaf *leaf = hanka_add_leaf(a, id, HANKA_LEAF_TEXT, w, h);
  if (!leaf) return 0;
  leaf->text = text;
  return 1;
}

static inline int hanka_slot_button(LukeArena *a, LukeText id, double w, double h, LukeText text) {
  HankaLeaf *leaf = hanka_add_leaf(a, id, HANKA_LEAF_BUTTON, w, h);
  if (!leaf) return 0;
  leaf->text = text;
  return 1;
}

static inline int hanka_slot_image(LukeArena *a, LukeText id, double w, double h, LukeText src) {
  HankaLeaf *leaf = hanka_add_leaf(a, id, HANKA_LEAF_IMAGE, w, h);
  if (!leaf) return 0;
  leaf->src = src;
  return 1;
}

static inline int hanka_slot_box(LukeArena *a, LukeText id, double w, double h) {
  return hanka_add_leaf(a, id, HANKA_LEAF_BOX, w, h) ? 1 : 0;
}

static inline int hanka_slot_text_at(LukeArena *a, LukeText id, double ox, double oy, double w,
                                     double h, LukeText text) {
  HankaLeaf *leaf = hanka_add_leaf(a, id, HANKA_LEAF_TEXT, w, h);
  if (!leaf) return 0;
  leaf->ox = ox;
  leaf->oy = oy;
  leaf->has_offset = 1;
  leaf->text = text;
  return 1;
}

static inline int hanka_slot_button_at(LukeArena *a, LukeText id, double ox, double oy, double w,
                                       double h, LukeText text) {
  HankaLeaf *leaf = hanka_add_leaf(a, id, HANKA_LEAF_BUTTON, w, h);
  if (!leaf) return 0;
  leaf->ox = ox;
  leaf->oy = oy;
  leaf->has_offset = 1;
  leaf->text = text;
  return 1;
}

static inline int hanka_slot_image_at(LukeArena *a, LukeText id, double ox, double oy, double w,
                                      double h, LukeText src) {
  HankaLeaf *leaf = hanka_add_leaf(a, id, HANKA_LEAF_IMAGE, w, h);
  if (!leaf) return 0;
  leaf->ox = ox;
  leaf->oy = oy;
  leaf->has_offset = 1;
  leaf->src = src;
  return 1;
}

static inline int hanka_slot_box_at(LukeArena *a, LukeText id, double ox, double oy, double w,
                                    double h) {
  HankaLeaf *leaf = hanka_add_leaf(a, id, HANKA_LEAF_BOX, w, h);
  if (!leaf) return 0;
  leaf->ox = ox;
  leaf->oy = oy;
  leaf->has_offset = 1;
  return 1;
}

static inline int hanka_end(LukeArena *a) {
  HankaState *s = hanka_state(a);
  if (!s->open) return 0;
  s->open = NULL;
  return 1;
}

static inline void hanka_place_leaf(LukeArena *a, const HankaLeaf *leaf, double x, double y) {
  LukeText id = luke_text(leaf->id);
  if (leaf->kind == HANKA_LEAF_TEXT)
    argus_place_text(a, id, x, y, leaf->w, leaf->h, leaf->text);
  else if (leaf->kind == HANKA_LEAF_BUTTON)
    argus_place_button(a, id, x, y, leaf->w, leaf->h, leaf->text);
  else if (leaf->kind == HANKA_LEAF_IMAGE)
    argus_place_image(a, id, x, y, leaf->w, leaf->h, leaf->src);
  else
    argus_place_box(a, id, x, y, leaf->w, leaf->h);
}

static inline void hanka_layout_box(LukeArena *a, const HankaBox *b) {
  double cx = b->x + b->pad;
  double cy = b->y + b->pad;
  for (size_t i = 0; i < b->len; ++i) {
    const HankaLeaf *leaf = &b->leaves[i];
    double lx, ly;
    if (b->axis == HANKA_STACK) {
      lx = b->x + (leaf->has_offset ? leaf->ox : b->pad);
      ly = b->y + (leaf->has_offset ? leaf->oy : b->pad);
    } else if (b->axis == HANKA_ROW) {
      lx = cx;
      ly = cy;
      cx += leaf->w + b->gap;
    } else { /* COLUMN */
      lx = cx;
      ly = cy;
      cy += leaf->h + b->gap;
    }
    hanka_place_leaf(a, leaf, lx, ly);
  }
}

/* Resolve all closed boxes into Argus frames. Open box is auto-ended. */
static inline int hanka_layout(LukeArena *a) {
  HankaState *s = hanka_state(a);
  if (s->open) hanka_end(a);
  for (size_t i = 0; i < s->box_len; ++i) hanka_layout_box(a, &s->boxes[i]);
  s->box_len = 0;
  return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* HANKA_H */
