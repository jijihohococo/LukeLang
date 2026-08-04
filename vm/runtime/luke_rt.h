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

#ifdef __cplusplus
}
#endif

#endif /* LUKE_RT_H */
