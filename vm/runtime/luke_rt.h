#ifndef LUKE_RT_H
#define LUKE_RT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tiny no-GC runtime for Luke Build mode.
 * Memory: bump arena. No mark-sweep. No tracing. */

typedef struct LukeText {
  const char *ptr;
  size_t len;
} LukeText;

typedef struct LukeArena {
  char *buf;
  size_t cap;
  size_t len;
} LukeArena;

/* Process argv — set once from main(). */
static int luke_rt_argc = 0;
static char **luke_rt_argv = NULL;

static inline void luke_runtime_set_args(int argc, char **argv) {
  luke_rt_argc = argc;
  luke_rt_argv = argv;
}

static inline int luke_arg_count(void) { return luke_rt_argc; }

static inline LukeText luke_text(const char *s) {
  LukeText t;
  t.ptr = s ? s : "";
  t.len = s ? strlen(s) : 0;
  return t;
}

static inline LukeText luke_text_n(const char *s, size_t n) {
  LukeText t;
  t.ptr = s ? s : "";
  t.len = n;
  return t;
}

static inline void luke_arena_init(LukeArena *a, size_t cap) {
  a->cap = cap ? cap : (1u << 20);
  a->len = 0;
  a->buf = (char *)malloc(a->cap);
  if (!a->buf) {
    fprintf(stderr, "Luke arena: out of memory\n");
    abort();
  }
}

static inline void luke_arena_free(LukeArena *a) {
  free(a->buf);
  a->buf = NULL;
  a->cap = a->len = 0;
}

/* Checkpoint / restore — IN ARENA scopes (bulk free back to mark). */
typedef size_t LukeArenaMark;

static inline LukeArenaMark luke_arena_mark(LukeArena *a) { return a->len; }

static inline void luke_arena_reset(LukeArena *a, LukeArenaMark m) {
  if (m <= a->len) a->len = m;
}

static inline void *luke_arena_alloc(LukeArena *a, size_t n, size_t align) {
  size_t mask = align - 1;
  size_t off = (a->len + mask) & ~mask;
  if (off + n > a->cap) {
    size_t need = off + n;
    size_t ncap = a->cap ? a->cap : 1024;
    while (ncap < need) ncap *= 2;
    char *nb = (char *)realloc(a->buf, ncap);
    if (!nb) {
      fprintf(stderr, "Luke arena: out of memory\n");
      abort();
    }
    a->buf = nb;
    a->cap = ncap;
  }
  void *p = a->buf + off;
  a->len = off + n;
  return p;
}

static inline LukeText luke_text_concat(LukeArena *a, LukeText x, LukeText y) {
  char *p = (char *)luke_arena_alloc(a, x.len + y.len + 1, 1);
  memcpy(p, x.ptr, x.len);
  memcpy(p + x.len, y.ptr, y.len);
  p[x.len + y.len] = '\0';
  return luke_text_n(p, x.len + y.len);
}

static inline void luke_speak_text(LukeText t) {
  fwrite(t.ptr, 1, t.len, stdout);
  fputc('\n', stdout);
}

static inline void luke_speak_number(double n) {
  /* Trim trailing zeros for friendlier output */
  char buf[64];
  snprintf(buf, sizeof(buf), "%.10g", n);
  puts(buf);
}

static inline void luke_speak_flag(int f) {
  puts(f ? "true" : "false");
}

/* ---------- collections (arena-backed; Luke LIST / MAP) ---------- */

typedef struct LukeList {
  LukeText *items;
  size_t len;
  size_t cap;
} LukeList;

typedef struct LukeMap {
  LukeText *keys;
  LukeText *vals;
  size_t len;
  size_t cap;
} LukeMap;

static inline LukeList *luke_list_new(LukeArena *a) {
  LukeList *l = (LukeList *)luke_arena_alloc(a, sizeof(LukeList), sizeof(void *));
  l->items = NULL;
  l->len = 0;
  l->cap = 0;
  return l;
}

static inline void luke_list_add(LukeArena *a, LukeList *l, LukeText v) {
  if (!l) return;
  if (l->len + 1 > l->cap) {
    size_t ncap = l->cap ? l->cap * 2 : 4;
    LukeText *ni = (LukeText *)luke_arena_alloc(a, ncap * sizeof(LukeText), sizeof(void *));
    if (l->items && l->len) memcpy(ni, l->items, l->len * sizeof(LukeText));
    l->items = ni;
    l->cap = ncap;
  }
  l->items[l->len++] = v;
}

static inline LukeText luke_list_get(LukeList *l, double index) {
  if (!l || index < 0) return luke_text("");
  size_t i = (size_t)index;
  if (i >= l->len) return luke_text("");
  return l->items[i];
}

static inline double luke_list_len(LukeList *l) { return l ? (double)l->len : 0; }

static inline LukeMap *luke_map_new(LukeArena *a) {
  LukeMap *m = (LukeMap *)luke_arena_alloc(a, sizeof(LukeMap), sizeof(void *));
  m->keys = NULL;
  m->vals = NULL;
  m->len = 0;
  m->cap = 0;
  return m;
}

static inline int luke_map_key_eq(LukeText a, LukeText b) {
  return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

static inline void luke_map_put(LukeArena *a, LukeMap *m, LukeText key, LukeText val) {
  if (!m) return;
  for (size_t i = 0; i < m->len; ++i) {
    if (luke_map_key_eq(m->keys[i], key)) {
      m->vals[i] = val;
      return;
    }
  }
  if (m->len + 1 > m->cap) {
    size_t ncap = m->cap ? m->cap * 2 : 4;
    LukeText *nk = (LukeText *)luke_arena_alloc(a, ncap * sizeof(LukeText), sizeof(void *));
    LukeText *nv = (LukeText *)luke_arena_alloc(a, ncap * sizeof(LukeText), sizeof(void *));
    if (m->keys && m->len) {
      memcpy(nk, m->keys, m->len * sizeof(LukeText));
      memcpy(nv, m->vals, m->len * sizeof(LukeText));
    }
    m->keys = nk;
    m->vals = nv;
    m->cap = ncap;
  }
  m->keys[m->len] = key;
  m->vals[m->len] = val;
  m->len++;
}

static inline LukeText luke_map_get(LukeMap *m, LukeText key) {
  if (!m) return luke_text("");
  for (size_t i = 0; i < m->len; ++i)
    if (luke_map_key_eq(m->keys[i], key)) return m->vals[i];
  return luke_text("");
}

static inline int luke_map_has(LukeMap *m, LukeText key) {
  if (!m) return 0;
  for (size_t i = 0; i < m->len; ++i)
    if (luke_map_key_eq(m->keys[i], key)) return 1;
  return 0;
}

static inline double luke_map_len(LukeMap *m) { return m ? (double)m->len : 0; }

/* Problem / last-error for conversational ATTEMPT */
static LukeText luke_last_problem = { "", 0 };
static inline void luke_set_problem(LukeText msg) { luke_last_problem = msg; }
static inline LukeText luke_the_problem(void) { return luke_last_problem; }
static inline int luke_has_problem(void) { return luke_last_problem.len > 0; }
static inline void luke_clear_problem(void) { luke_last_problem = luke_text(""); }

#ifdef __cplusplus
}
#endif

#endif /* LUKE_RT_H */
