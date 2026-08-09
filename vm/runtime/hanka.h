#ifndef HANKA_H
#define HANKA_H

/* Hanka — LukeLang layout engine.
 * Owns layout numbers (x,y,w,h). Nested COLUMN/ROW/STACK trees supported.
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
  HANKA_LEAF_IMAGE = 3,
  HANKA_LEAF_INPUT = 4
} HankaLeafKind;

typedef struct HankaLeaf {
  char id[64];
  HankaLeafKind kind;
  double w, h;
  double ox, oy;
  int has_offset;
  int input_type; /* 0 text, 1 password, 2 email */
  LukeText text;
  LukeText src;
} HankaLeaf;

typedef struct HankaBox HankaBox;

typedef enum HankaChildKind {
  HANKA_CHILD_LEAF = 0,
  HANKA_CHILD_BOX = 1
} HankaChildKind;

typedef struct HankaChild {
  HankaChildKind kind;
  HankaLeaf leaf;
  HankaBox *box;
} HankaChild;

struct HankaBox {
  HankaAxis axis;
  double x, y, w, h;
  double pad, gap;
  HankaChild *children;
  size_t len;
  size_t cap;
  HankaBox *parent;
  int is_root;
};

typedef struct HankaState {
  LukeArena *arena;
  HankaBox *roots[64];
  size_t root_len;
  HankaBox *stack[32];
  size_t depth;
} HankaState;

static HankaState hanka_global = {0};

static inline void hanka_id_copy(char *dst, size_t cap, LukeText id) {
  size_t n = id.len < cap - 1 ? id.len : cap - 1;
  if (n && id.ptr) memcpy(dst, id.ptr, n);
  dst[n] = '\0';
}

static inline HankaState *hanka_state(LukeArena *a) {
  if (!hanka_global.arena) {
    memset(&hanka_global, 0, sizeof(hanka_global));
    hanka_global.arena = a;
  }
  return &hanka_global;
}

static inline void hanka_clear(LukeArena *a) {
  HankaState *s = hanka_state(a);
  s->root_len = 0;
  s->depth = 0;
}

static inline int hanka_add_child(LukeArena *a, HankaBox *b, HankaChild child) {
  if (!b) return 0;
  if (b->len + 1 > b->cap) {
    size_t ncap = b->cap ? b->cap * 2 : 8;
    HankaChild *nc = (HankaChild *)luke_arena_alloc(a, ncap * sizeof(HankaChild), sizeof(void *));
    if (b->children && b->len) memcpy(nc, b->children, b->len * sizeof(HankaChild));
    b->children = nc;
    b->cap = ncap;
  }
  b->children[b->len++] = child;
  return 1;
}

static inline HankaBox *hanka_push_box(LukeArena *a, HankaAxis axis, double x, double y, double w,
                                       double h, double pad, double gap) {
  HankaState *s = hanka_state(a);
  HankaBox *b = (HankaBox *)luke_arena_alloc(a, sizeof(HankaBox), sizeof(void *));
  memset(b, 0, sizeof(*b));
  b->axis = axis;
  b->x = x;
  b->y = y;
  b->w = w;
  b->h = h;
  b->pad = pad;
  b->gap = gap;
  if (s->depth > 0) {
    HankaBox *parent = s->stack[s->depth - 1];
    b->parent = parent;
    HankaChild ch;
    memset(&ch, 0, sizeof(ch));
    ch.kind = HANKA_CHILD_BOX;
    ch.box = b;
    hanka_add_child(a, parent, ch);
  } else {
    b->is_root = 1;
    if (s->root_len < 64) s->roots[s->root_len++] = b;
  }
  if (s->depth < 32) s->stack[s->depth++] = b;
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

static inline HankaBox *hanka_open(LukeArena *a) {
  HankaState *s = hanka_state(a);
  if (!s->depth) return NULL;
  return s->stack[s->depth - 1];
}

static inline HankaLeaf *hanka_add_leaf(LukeArena *a, LukeText id, HankaLeafKind kind, double w,
                                        double h) {
  HankaBox *b = hanka_open(a);
  if (!b) return NULL;
  HankaChild ch;
  memset(&ch, 0, sizeof(ch));
  ch.kind = HANKA_CHILD_LEAF;
  hanka_id_copy(ch.leaf.id, sizeof(ch.leaf.id), id);
  ch.leaf.kind = kind;
  ch.leaf.w = w;
  ch.leaf.h = h;
  if (!hanka_add_child(a, b, ch)) return NULL;
  return &b->children[b->len - 1].leaf;
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

static inline int hanka_slot_input(LukeArena *a, LukeText id, double w, double h,
                                   LukeText placeholder, double input_type) {
  HankaLeaf *leaf = hanka_add_leaf(a, id, HANKA_LEAF_INPUT, w, h);
  if (!leaf) return 0;
  leaf->text = placeholder;
  leaf->input_type = (int)input_type;
  return 1;
}

static inline int hanka_slot_input_at(LukeArena *a, LukeText id, double ox, double oy, double w,
                                      double h, LukeText placeholder, double input_type) {
  HankaLeaf *leaf = hanka_add_leaf(a, id, HANKA_LEAF_INPUT, w, h);
  if (!leaf) return 0;
  leaf->ox = ox;
  leaf->oy = oy;
  leaf->has_offset = 1;
  leaf->text = placeholder;
  leaf->input_type = (int)input_type;
  return 1;
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
  if (!s->depth) return 0;
  s->depth--;
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
  else if (leaf->kind == HANKA_LEAF_INPUT)
    argus_place_input(a, id, x, y, leaf->w, leaf->h, leaf->text, (double)leaf->input_type);
  else
    argus_place_box(a, id, x, y, leaf->w, leaf->h);
}

static inline void hanka_layout_box_at(LukeArena *a, HankaBox *b, double abs_x, double abs_y);

static inline void hanka_layout_box_at(LukeArena *a, HankaBox *b, double abs_x, double abs_y) {
  double cx = abs_x + b->pad;
  double cy = abs_y + b->pad;
  for (size_t i = 0; i < b->len; ++i) {
    HankaChild *ch = &b->children[i];
    if (ch->kind == HANKA_CHILD_LEAF) {
      HankaLeaf *leaf = &ch->leaf;
      double lx, ly;
      if (b->axis == HANKA_STACK) {
        lx = abs_x + (leaf->has_offset ? leaf->ox : b->pad);
        ly = abs_y + (leaf->has_offset ? leaf->oy : b->pad);
      } else if (b->axis == HANKA_ROW) {
        lx = cx;
        ly = cy;
        cx += leaf->w + b->gap;
      } else {
        lx = cx;
        ly = cy;
        cy += leaf->h + b->gap;
      }
      hanka_place_leaf(a, leaf, lx, ly);
    } else if (ch->box) {
      HankaBox *child = ch->box;
      double lx, ly;
      if (b->axis == HANKA_STACK) {
        lx = abs_x + child->x;
        ly = abs_y + child->y;
      } else if (b->axis == HANKA_ROW) {
        lx = cx;
        ly = cy;
        cx += child->w + b->gap;
      } else {
        lx = cx;
        ly = cy;
        cy += child->h + b->gap;
      }
      hanka_layout_box_at(a, child, lx, ly);
    }
  }
}

static inline int hanka_layout(LukeArena *a) {
  HankaState *s = hanka_state(a);
  while (s->depth) hanka_end(a);
  for (size_t i = 0; i < s->root_len; ++i) {
    HankaBox *b = s->roots[i];
    hanka_layout_box_at(a, b, b->x, b->y);
  }
  s->root_len = 0;
  return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* HANKA_H */
