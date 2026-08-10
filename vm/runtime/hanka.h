#ifndef HANKA_H
#define HANKA_H

/* Hanka — LukeLang layout engine.
 * Path A: ROW/COLUMN emit CSS flex containers (browser lays out).
 * STACK / explicit PLACE still use absolute frames into Argus. See docs/HANKA.md */

#include "argus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum HankaAxis {
  HANKA_COLUMN = 0,
  HANKA_ROW = 1,
  HANKA_STACK = 2
} HankaAxis;

typedef enum HankaAlign {
  HANKA_ALIGN_START = 0,
  HANKA_ALIGN_CENTER = 1,
  HANKA_ALIGN_END = 2
} HankaAlign;

typedef enum HankaLeafKind {
  HANKA_LEAF_BOX = 0,
  HANKA_LEAF_TEXT = 1,
  HANKA_LEAF_BUTTON = 2,
  HANKA_LEAF_IMAGE = 3,
  HANKA_LEAF_INPUT = 4,
  HANKA_LEAF_SELECT = 5,
  HANKA_LEAF_TABLE = 6,
  HANKA_LEAF_MODAL = 7
} HankaLeafKind;

typedef struct HankaLeaf {
  char id[64];
  HankaLeafKind kind;
  double w, h;
  double ox, oy;
  int has_offset;
  int input_type; /* 0 text, 1 password, 2 email, 3 checkbox, 4 radio */
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
  char id[64]; /* Path A — stable flex container id (__hk_N) */
  double x, y, w, h;
  double pad, gap;
  int align; /* 0=start, 1=center, 2=end */
  int wrap;  /* 1 = wrap main axis onto next cross line/column */
  HankaChild *children;
  size_t len;
  size_t cap;
  HankaBox *parent;
  int is_root;
  int dirty; /* Phase 11 — region relayout when set on root */
};

typedef struct HankaState {
  LukeArena *arena;
  HankaBox *roots[64];
  size_t root_len;
  HankaBox *stack[32];
  size_t depth;
  int keep_roots; /* retain layout tree for partial relayout */
  int next_box_id;
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
  s->next_box_id = 0;
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

static inline void hanka_assign_box_id(HankaState *s, HankaBox *b) {
  char buf[64];
  int n = snprintf(buf, sizeof(buf), "__hk_%d", s->next_box_id++);
  if (n < 0) n = 0;
  if (n >= (int)sizeof(b->id)) n = (int)sizeof(b->id) - 1;
  memcpy(b->id, buf, (size_t)n);
  b->id[n] = '\0';
}

static inline HankaBox *hanka_push_box(LukeArena *a, HankaAxis axis, double x, double y, double w,
                                       double h, double pad, double gap) {
  HankaState *s = hanka_state(a);
  HankaBox *b = (HankaBox *)luke_arena_alloc(a, sizeof(HankaBox), sizeof(void *));
  memset(b, 0, sizeof(*b));
  b->axis = axis;
  hanka_assign_box_id(s, b);
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

static inline int hanka_set_align(LukeArena *a, double align) {
  HankaBox *b = hanka_open(a);
  if (!b) return 0;
  int v = (int)align;
  if (v < 0) v = 0;
  if (v > 2) v = 2;
  b->align = v;
  return 1;
}

static inline int hanka_set_wrap(LukeArena *a, double wrap) {
  HankaBox *b = hanka_open(a);
  if (!b) return 0;
  b->wrap = wrap != 0.0 ? 1 : 0;
  return 1;
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

static inline int hanka_slot_select(LukeArena *a, LukeText id, double w, double h,
                                    LukeText options) {
  HankaLeaf *leaf = hanka_add_leaf(a, id, HANKA_LEAF_SELECT, w, h);
  if (!leaf) return 0;
  leaf->text = options;
  return 1;
}

static inline int hanka_slot_table(LukeArena *a, LukeText id, double w, double h, LukeText cells) {
  HankaLeaf *leaf = hanka_add_leaf(a, id, HANKA_LEAF_TABLE, w, h);
  if (!leaf) return 0;
  leaf->text = cells;
  return 1;
}

static inline int hanka_slot_modal(LukeArena *a, LukeText id, double w, double h, LukeText text) {
  HankaLeaf *leaf = hanka_add_leaf(a, id, HANKA_LEAF_MODAL, w, h);
  if (!leaf) return 0;
  leaf->text = text;
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

static inline void hanka_resolve_leaf_size(HankaLeaf *leaf) {
  if (!leaf) return;
  if (leaf->w < 0 && (leaf->kind == HANKA_LEAF_TEXT || leaf->kind == HANKA_LEAF_BUTTON ||
                      leaf->kind == HANKA_LEAF_MODAL)) {
    leaf->w = argus_measure_text(leaf->text);
    if (leaf->w < 24) leaf->w = 24;
  }
  if (leaf->h < 0) leaf->h = 32;
}

static inline int hanka_box_has_leaf(HankaBox *b, const char *leaf_id) {
  if (!b || !leaf_id || !leaf_id[0]) return 0;
  for (size_t i = 0; i < b->len; ++i) {
    HankaChild *ch = &b->children[i];
    if (ch->kind == HANKA_CHILD_LEAF) {
      if (strcmp(ch->leaf.id, leaf_id) == 0) return 1;
    } else if (ch->box && hanka_box_has_leaf(ch->box, leaf_id)) return 1;
  }
  return 0;
}

static inline void hanka_set_keep_roots(LukeArena *a, int keep) {
  hanka_state(a)->keep_roots = keep ? 1 : 0;
}

static inline int hanka_mark_region(LukeArena *a, LukeText leaf_id) {
  HankaState *s = hanka_state(a);
  char buf[64];
  hanka_id_copy(buf, sizeof(buf), leaf_id);
  for (size_t i = 0; i < s->root_len; ++i) {
    if (hanka_box_has_leaf(s->roots[i], buf)) {
      s->roots[i]->dirty = 1;
      return 1;
    }
  }
  return 0;
}

static inline void hanka_place_leaf(LukeArena *a, const HankaLeaf *leaf, double x, double y) {
  LukeText id = luke_text(leaf->id);
  double w = leaf->w;
  double h = leaf->h;
  if (leaf->kind == HANKA_LEAF_TEXT)
    argus_place_text(a, id, x, y, w, h, leaf->text);
  else if (leaf->kind == HANKA_LEAF_BUTTON)
    argus_place_button(a, id, x, y, w, h, leaf->text);
  else if (leaf->kind == HANKA_LEAF_IMAGE)
    argus_place_image(a, id, x, y, w, h, leaf->src);
  else if (leaf->kind == HANKA_LEAF_INPUT)
    argus_place_input(a, id, x, y, w, h, leaf->text, (double)leaf->input_type);
  else if (leaf->kind == HANKA_LEAF_SELECT)
    argus_place_select(a, id, x, y, w, h, leaf->text);
  else if (leaf->kind == HANKA_LEAF_TABLE)
    argus_place_table(a, id, x, y, w, h, leaf->text);
  else if (leaf->kind == HANKA_LEAF_MODAL)
    argus_place_modal(a, id, x, y, w, h, leaf->text);
  else
    argus_place_box(a, id, x, y, w, h);
}

static inline void hanka_place_flow_leaf(LukeArena *a, const HankaLeaf *leaf, LukeText parent) {
  LukeText id = luke_text(leaf->id);
  double w = leaf->w;
  double h = leaf->h;
  if (leaf->kind == HANKA_LEAF_TEXT)
    argus_place_flow_text(a, id, parent, w, h, leaf->text);
  else if (leaf->kind == HANKA_LEAF_BUTTON)
    argus_place_flow_button(a, id, parent, w, h, leaf->text);
  else if (leaf->kind == HANKA_LEAF_IMAGE)
    argus_place_flow_image(a, id, parent, w, h, leaf->src);
  else if (leaf->kind == HANKA_LEAF_INPUT)
    argus_place_flow_input(a, id, parent, w, h, leaf->text, (double)leaf->input_type);
  else if (leaf->kind == HANKA_LEAF_SELECT)
    argus_place_flow_select(a, id, parent, w, h, leaf->text);
  else if (leaf->kind == HANKA_LEAF_TABLE)
    argus_place_flow_table(a, id, parent, w, h, leaf->text);
  else if (leaf->kind == HANKA_LEAF_MODAL)
    argus_place_flow_modal(a, id, parent, w, h, leaf->text);
  else
    argus_place_flow_box(a, id, parent, w, h);
}

static inline void hanka_layout_box_at(LukeArena *a, HankaBox *b, double abs_x, double abs_y);

static inline double hanka_child_main(const HankaChild *ch, int row) {
  if (ch->kind == HANKA_CHILD_LEAF) return row ? ch->leaf.w : ch->leaf.h;
  if (ch->box) return row ? ch->box->w : ch->box->h;
  return 0;
}

static inline double hanka_child_cross(const HankaChild *ch, int row) {
  if (ch->kind == HANKA_CHILD_LEAF) return row ? ch->leaf.h : ch->leaf.w;
  if (ch->box) return row ? ch->box->h : ch->box->w;
  return 0;
}

static inline double hanka_align_offset(int align, double free_space) {
  if (free_space <= 0) return 0;
  if (align == HANKA_ALIGN_CENTER) return free_space * 0.5;
  if (align == HANKA_ALIGN_END) return free_space;
  return 0;
}

/* Path A: ROW/COLUMN → flex container + normal-flow children (browser does layout). */
static inline void hanka_layout_flex_box(LukeArena *a, HankaBox *b, double abs_x, double abs_y) {
  for (size_t i = 0; i < b->len; ++i) {
    if (b->children[i].kind == HANKA_CHILD_LEAF) hanka_resolve_leaf_size(&b->children[i].leaf);
  }
  LukeText box_id = luke_text(b->id);
  int under_flex_parent = b->parent && b->parent->axis != HANKA_STACK;
  int absolute = !under_flex_parent;
  LukeText parent_id = under_flex_parent ? luke_text(b->parent->id) : luke_text("");
  int dir = b->axis == HANKA_ROW ? 2 : 1;
  argus_place_flex(a, box_id, parent_id, absolute, absolute ? abs_x : 0.0, absolute ? abs_y : 0.0,
                   b->w, b->h, dir, b->gap, b->pad, b->align, b->wrap);
  for (size_t i = 0; i < b->len; ++i) {
    HankaChild *ch = &b->children[i];
    if (ch->kind == HANKA_CHILD_LEAF) {
      hanka_place_flow_leaf(a, &ch->leaf, box_id);
    } else if (ch->box) {
      hanka_layout_box_at(a, ch->box, 0, 0);
    }
  }
}

static inline void hanka_layout_box_at(LukeArena *a, HankaBox *b, double abs_x, double abs_y) {
  if (b->axis == HANKA_COLUMN || b->axis == HANKA_ROW) {
    hanka_layout_flex_box(a, b, abs_x, abs_y);
    return;
  }

  double inner_w = b->w - 2 * b->pad;
  double inner_h = b->h - 2 * b->pad;
  for (size_t i = 0; i < b->len; ++i) {
    if (b->children[i].kind == HANKA_CHILD_LEAF) hanka_resolve_leaf_size(&b->children[i].leaf);
  }
  if (b->axis == HANKA_STACK) {
    for (size_t i = 0; i < b->len; ++i) {
      HankaChild *ch = &b->children[i];
      if (ch->kind == HANKA_CHILD_LEAF) {
        HankaLeaf *leaf = &ch->leaf;
        double lx = abs_x + (leaf->has_offset ? leaf->ox : b->pad);
        double ly = abs_y + (leaf->has_offset ? leaf->oy : b->pad);
        hanka_place_leaf(a, leaf, lx, ly);
      } else if (ch->box) {
        hanka_layout_box_at(a, ch->box, abs_x + ch->box->x, abs_y + ch->box->y);
      }
    }
    return;
  }

  /* Unreachable: ROW/COLUMN handled above; keep classic absolute as fallback. */
  int row = b->axis == HANKA_ROW;
  if (b->wrap) {
    double main_limit = row ? inner_w : inner_h;
    double cx = abs_x + b->pad;
    double cy = abs_y + b->pad;
    double used_main = 0;
    double max_cross = 0;
    for (size_t i = 0; i < b->len; ++i) {
      HankaChild *ch = &b->children[i];
      double main = hanka_child_main(ch, row);
      double cross = hanka_child_cross(ch, row);
      if (used_main > 0 && used_main + b->gap + main > main_limit + 0.5) {
        if (row) {
          cy += max_cross + b->gap;
          cx = abs_x + b->pad;
        } else {
          cx += max_cross + b->gap;
          cy = abs_y + b->pad;
        }
        used_main = 0;
        max_cross = 0;
      }
      if (used_main > 0) {
        if (row) cx += b->gap;
        else cy += b->gap;
        used_main += b->gap;
      }
      if (ch->kind == HANKA_CHILD_LEAF) {
        hanka_place_leaf(a, &ch->leaf, cx, cy);
      } else if (ch->box) {
        hanka_layout_box_at(a, ch->box, cx, cy);
      }
      if (row) cx += main;
      else cy += main;
      used_main += main;
      if (cross > max_cross) max_cross = cross;
    }
    return;
  }

  double main_total = 0;
  for (size_t i = 0; i < b->len; ++i) {
    main_total += hanka_child_main(&b->children[i], row);
    if (i + 1 < b->len) main_total += b->gap;
  }
  double main_free = (row ? inner_w : inner_h) - main_total;
  double main_off = hanka_align_offset(b->align, main_free);
  double cx = abs_x + b->pad + (row ? main_off : 0);
  double cy = abs_y + b->pad + (row ? 0 : main_off);

  for (size_t i = 0; i < b->len; ++i) {
    HankaChild *ch = &b->children[i];
    double cross = hanka_child_cross(ch, row);
    double cross_free = (row ? inner_h : inner_w) - cross;
    double cross_off = hanka_align_offset(b->align, cross_free);
    if (ch->kind == HANKA_CHILD_LEAF) {
      HankaLeaf *leaf = &ch->leaf;
      double lx = row ? cx : (cx + cross_off);
      double ly = row ? (cy + cross_off) : cy;
      hanka_place_leaf(a, leaf, lx, ly);
      if (row) cx += leaf->w + b->gap;
      else cy += leaf->h + b->gap;
    } else if (ch->box) {
      HankaBox *child = ch->box;
      double lx = row ? cx : (cx + cross_off);
      double ly = row ? (cy + cross_off) : cy;
      hanka_layout_box_at(a, child, lx, ly);
      if (row) cx += child->w + b->gap;
      else cy += child->h + b->gap;
    }
  }
}

static inline int hanka_layout(LukeArena *a) {
  HankaState *s = hanka_state(a);
  while (s->depth) hanka_end(a);
  for (size_t i = 0; i < s->root_len; ++i) {
    HankaBox *b = s->roots[i];
    hanka_layout_box_at(a, b, b->x, b->y);
    b->dirty = 0;
  }
  if (!s->keep_roots) s->root_len = 0;
  return 1;
}

/* Relayout only roots marked dirty (Granularity / Milestone C). */
static inline int hanka_layout_dirty(LukeArena *a) {
  HankaState *s = hanka_state(a);
  int count = 0;
  for (size_t i = 0; i < s->root_len; ++i) {
    HankaBox *b = s->roots[i];
    if (!b->dirty) continue;
    hanka_layout_box_at(a, b, b->x, b->y);
    b->dirty = 0;
    count++;
  }
  return count;
}

#ifdef __cplusplus
}
#endif

#endif /* HANKA_H */
