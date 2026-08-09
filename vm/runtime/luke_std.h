#ifndef LUKE_STD_H
#define LUKE_STD_H

/* Thin Build-mode standard library helpers (no GC).
 * Linked by including this header after luke_rt.h in generated C. */

#include "luke_rt.h"
#include "luke_net.h"
#include "luke_db.h"
#include "luke_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if !defined(__wasi__)
/* popen/pclose for httpGet — need POSIX decls under pedantic C. */
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- files ---------- */

static inline LukeText luke_read_file(LukeArena *a, LukeText path) {
  char name[1024];
  size_t n = path.len < sizeof(name) - 1 ? path.len : sizeof(name) - 1;
  memcpy(name, path.ptr, n);
  name[n] = '\0';
  FILE *f = fopen(name, "rb");
  if (!f) return luke_text("");
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return luke_text("");
  }
  long sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return luke_text("");
  }
  rewind(f);
  char *buf = (char *)luke_arena_alloc(a, (size_t)sz + 1, 1);
  size_t got = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[got] = '\0';
  return luke_text_n(buf, got);
}

static inline int luke_write_file(LukeText path, LukeText data) {
  char name[1024];
  size_t n = path.len < sizeof(name) - 1 ? path.len : sizeof(name) - 1;
  memcpy(name, path.ptr, n);
  name[n] = '\0';
  FILE *f = fopen(name, "wb");
  if (!f) return 0;
  size_t w = fwrite(data.ptr, 1, data.len, f);
  fclose(f);
  return w == data.len ? 1 : 0;
}

static inline int luke_file_exists(LukeText path) {
  char name[1024];
  size_t n = path.len < sizeof(name) - 1 ? path.len : sizeof(name) - 1;
  memcpy(name, path.ptr, n);
  name[n] = '\0';
  FILE *f = fopen(name, "rb");
  if (!f) return 0;
  fclose(f);
  return 1;
}

/* ---------- JSON (arena-backed tree) ---------- */

typedef enum {
  LUKE_JSON_NULL = 0,
  LUKE_JSON_BOOL,
  LUKE_JSON_NUMBER,
  LUKE_JSON_STRING,
  LUKE_JSON_ARRAY,
  LUKE_JSON_OBJECT
} LukeJsonKind;

typedef struct LukeJson LukeJson;

struct LukeJson {
  LukeJsonKind kind;
  int boolean;
  double number;
  LukeText string;
  LukeJson **items;
  size_t len;
  LukeText *keys;
};

typedef struct {
  const char *s;
  size_t n;
  size_t i;
  LukeArena *a;
  int ok;
} LukeJsonParser;

static inline void luke_json_skip(LukeJsonParser *p) {
  while (p->i < p->n) {
    char c = p->s[p->i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++p->i;
    else break;
  }
}

static inline LukeJson *luke_json_new(LukeArena *a, LukeJsonKind k) {
  LukeJson *v = (LukeJson *)luke_arena_alloc(a, sizeof(LukeJson), sizeof(void *));
  memset(v, 0, sizeof(*v));
  v->kind = k;
  return v;
}

static inline LukeJson *luke_json_parse_value(LukeJsonParser *p);

static inline LukeText luke_json_parse_string(LukeJsonParser *p) {
  if (p->i >= p->n || p->s[p->i] != '"') {
    p->ok = 0;
    return luke_text("");
  }
  ++p->i;
  size_t start = p->i;
  size_t outCap = 0;
  /* First pass: find end; second: unescape into arena */
  size_t j = p->i;
  while (j < p->n && p->s[j] != '"') {
    if (p->s[j] == '\\' && j + 1 < p->n) j += 2;
    else ++j;
  }
  if (j >= p->n) {
    p->ok = 0;
    return luke_text("");
  }
  outCap = j - start + 1;
  char *buf = (char *)luke_arena_alloc(p->a, outCap, 1);
  size_t o = 0;
  while (p->i < p->n && p->s[p->i] != '"') {
    if (p->s[p->i] == '\\' && p->i + 1 < p->n) {
      char e = p->s[p->i + 1];
      if (e == 'n') buf[o++] = '\n';
      else if (e == 't') buf[o++] = '\t';
      else if (e == 'r') buf[o++] = '\r';
      else if (e == '"' || e == '\\' || e == '/') buf[o++] = e;
      else buf[o++] = e;
      p->i += 2;
    } else {
      buf[o++] = p->s[p->i++];
    }
  }
  if (p->i >= p->n || p->s[p->i] != '"') {
    p->ok = 0;
    return luke_text("");
  }
  ++p->i;
  buf[o] = '\0';
  return luke_text_n(buf, o);
}

static inline LukeJson *luke_json_parse_object(LukeJsonParser *p) {
  LukeJson *obj = luke_json_new(p->a, LUKE_JSON_OBJECT);
  ++p->i; /* { */
  luke_json_skip(p);
  if (p->i < p->n && p->s[p->i] == '}') {
    ++p->i;
    return obj;
  }
  size_t cap = 4;
  obj->keys = (LukeText *)luke_arena_alloc(p->a, cap * sizeof(LukeText), sizeof(void *));
  obj->items = (LukeJson **)luke_arena_alloc(p->a, cap * sizeof(LukeJson *), sizeof(void *));
  obj->len = 0;
  for (;;) {
    luke_json_skip(p);
    LukeText key = luke_json_parse_string(p);
    if (!p->ok) return obj;
    luke_json_skip(p);
    if (p->i >= p->n || p->s[p->i] != ':') {
      p->ok = 0;
      return obj;
    }
    ++p->i;
    LukeJson *val = luke_json_parse_value(p);
    if (!p->ok) return obj;
    if (obj->len >= cap) {
      size_t ncap = cap * 2;
      LukeText *nk = (LukeText *)luke_arena_alloc(p->a, ncap * sizeof(LukeText), sizeof(void *));
      LukeJson **nv = (LukeJson **)luke_arena_alloc(p->a, ncap * sizeof(LukeJson *), sizeof(void *));
      memcpy(nk, obj->keys, obj->len * sizeof(LukeText));
      memcpy(nv, obj->items, obj->len * sizeof(LukeJson *));
      obj->keys = nk;
      obj->items = nv;
      cap = ncap;
    }
    obj->keys[obj->len] = key;
    obj->items[obj->len] = val;
    obj->len++;
    luke_json_skip(p);
    if (p->i < p->n && p->s[p->i] == ',') {
      ++p->i;
      continue;
    }
    if (p->i < p->n && p->s[p->i] == '}') {
      ++p->i;
      break;
    }
    p->ok = 0;
    break;
  }
  return obj;
}

static inline LukeJson *luke_json_parse_array(LukeJsonParser *p) {
  LukeJson *arr = luke_json_new(p->a, LUKE_JSON_ARRAY);
  ++p->i; /* [ */
  luke_json_skip(p);
  if (p->i < p->n && p->s[p->i] == ']') {
    ++p->i;
    return arr;
  }
  size_t cap = 4;
  arr->items = (LukeJson **)luke_arena_alloc(p->a, cap * sizeof(LukeJson *), sizeof(void *));
  arr->len = 0;
  for (;;) {
    LukeJson *val = luke_json_parse_value(p);
    if (!p->ok) return arr;
    if (arr->len >= cap) {
      size_t ncap = cap * 2;
      LukeJson **nv = (LukeJson **)luke_arena_alloc(p->a, ncap * sizeof(LukeJson *), sizeof(void *));
      memcpy(nv, arr->items, arr->len * sizeof(LukeJson *));
      arr->items = nv;
      cap = ncap;
    }
    arr->items[arr->len++] = val;
    luke_json_skip(p);
    if (p->i < p->n && p->s[p->i] == ',') {
      ++p->i;
      continue;
    }
    if (p->i < p->n && p->s[p->i] == ']') {
      ++p->i;
      break;
    }
    p->ok = 0;
    break;
  }
  return arr;
}

static inline LukeJson *luke_json_parse_value(LukeJsonParser *p) {
  luke_json_skip(p);
  if (!p->ok || p->i >= p->n) {
    p->ok = 0;
    return luke_json_new(p->a, LUKE_JSON_NULL);
  }
  char c = p->s[p->i];
  if (c == '{') return luke_json_parse_object(p);
  if (c == '[') return luke_json_parse_array(p);
  if (c == '"') {
    LukeJson *v = luke_json_new(p->a, LUKE_JSON_STRING);
    v->string = luke_json_parse_string(p);
    return v;
  }
  if (c == 't' && p->i + 4 <= p->n && memcmp(p->s + p->i, "true", 4) == 0) {
    p->i += 4;
    LukeJson *v = luke_json_new(p->a, LUKE_JSON_BOOL);
    v->boolean = 1;
    return v;
  }
  if (c == 'f' && p->i + 5 <= p->n && memcmp(p->s + p->i, "false", 5) == 0) {
    p->i += 5;
    LukeJson *v = luke_json_new(p->a, LUKE_JSON_BOOL);
    v->boolean = 0;
    return v;
  }
  if (c == 'n' && p->i + 4 <= p->n && memcmp(p->s + p->i, "null", 4) == 0) {
    p->i += 4;
    return luke_json_new(p->a, LUKE_JSON_NULL);
  }
  /* number */
  size_t start = p->i;
  if (c == '-' ) ++p->i;
  if (p->i >= p->n || !isdigit((unsigned char)p->s[p->i])) {
    p->ok = 0;
    return luke_json_new(p->a, LUKE_JSON_NULL);
  }
  while (p->i < p->n && isdigit((unsigned char)p->s[p->i])) ++p->i;
  if (p->i < p->n && p->s[p->i] == '.') {
    ++p->i;
    while (p->i < p->n && isdigit((unsigned char)p->s[p->i])) ++p->i;
  }
  if (p->i < p->n && (p->s[p->i] == 'e' || p->s[p->i] == 'E')) {
    ++p->i;
    if (p->i < p->n && (p->s[p->i] == '+' || p->s[p->i] == '-')) ++p->i;
    while (p->i < p->n && isdigit((unsigned char)p->s[p->i])) ++p->i;
  }
  char tmp[64];
  size_t len = p->i - start;
  if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
  memcpy(tmp, p->s + start, len);
  tmp[len] = '\0';
  LukeJson *v = luke_json_new(p->a, LUKE_JSON_NUMBER);
  v->number = atof(tmp);
  return v;
}

static inline LukeJson *luke_json_parse(LukeArena *a, LukeText text) {
  LukeJsonParser p;
  p.s = text.ptr;
  p.n = text.len;
  p.i = 0;
  p.a = a;
  p.ok = 1;
  LukeJson *v = luke_json_parse_value(&p);
  luke_json_skip(&p);
  if (!p.ok || p.i != p.n) {
    /* Still return whatever we built; callers can check is_null on total failure */
    if (!p.ok && (v == NULL || (v->kind == LUKE_JSON_NULL && text.len > 0 && text.ptr[0] != 'n'))) {
      return luke_json_new(a, LUKE_JSON_NULL);
    }
  }
  return v;
}

static inline int luke_json_key_eq(LukeText a, LukeText b) {
  return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

static inline LukeJson *luke_json_get(LukeJson *obj, LukeText key) {
  if (!obj || obj->kind != LUKE_JSON_OBJECT) return NULL;
  for (size_t i = 0; i < obj->len; ++i) {
    if (luke_json_key_eq(obj->keys[i], key)) return obj->items[i];
  }
  return NULL;
}

static inline LukeJson *luke_json_index(LukeJson *arr, double index) {
  if (!arr || arr->kind != LUKE_JSON_ARRAY) return NULL;
  if (index < 0) return NULL;
  size_t i = (size_t)index;
  if (i >= arr->len) return NULL;
  return arr->items[i];
}

static inline double luke_json_len(LukeJson *v) {
  if (!v) return 0;
  if (v->kind == LUKE_JSON_ARRAY || v->kind == LUKE_JSON_OBJECT) return (double)v->len;
  if (v->kind == LUKE_JSON_STRING) return (double)v->string.len;
  return 0;
}

static inline int luke_json_has(LukeJson *obj, LukeText key) {
  return luke_json_get(obj, key) != NULL ? 1 : 0;
}

static inline LukeText luke_json_as_text(LukeArena *a, LukeJson *v) {
  (void)a;
  if (!v) return luke_text("");
  if (v->kind == LUKE_JSON_STRING) return v->string;
  if (v->kind == LUKE_JSON_BOOL) return luke_text(v->boolean ? "true" : "false");
  if (v->kind == LUKE_JSON_NULL) return luke_text("null");
  if (v->kind == LUKE_JSON_NUMBER) {
    char buf[64];
    int k = snprintf(buf, sizeof(buf), "%.10g", v->number);
    if (k < 0) k = 0;
    char *p = (char *)luke_arena_alloc(a, (size_t)k + 1, 1);
    memcpy(p, buf, (size_t)k + 1);
    return luke_text_n(p, (size_t)k);
  }
  return luke_text("");
}

static inline double luke_json_as_number(LukeJson *v) {
  if (!v) return 0;
  if (v->kind == LUKE_JSON_NUMBER) return v->number;
  if (v->kind == LUKE_JSON_BOOL) return v->boolean ? 1.0 : 0.0;
  return 0;
}

static inline int luke_json_as_flag(LukeJson *v) {
  if (!v) return 0;
  if (v->kind == LUKE_JSON_BOOL) return v->boolean;
  if (v->kind == LUKE_JSON_NULL) return 0;
  if (v->kind == LUKE_JSON_NUMBER) return v->number != 0;
  return 1;
}

static inline int luke_json_is_null(LukeJson *v) {
  return (!v || v->kind == LUKE_JSON_NULL) ? 1 : 0;
}

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
  LukeArena *a;
} LukeJsonBuf;

static inline void luke_json_buf_init(LukeJsonBuf *b, LukeArena *a) {
  b->a = a;
  b->cap = 64;
  b->len = 0;
  b->buf = (char *)luke_arena_alloc(a, b->cap, 1);
  b->buf[0] = '\0';
}

static inline void luke_json_buf_grow(LukeJsonBuf *b, size_t need) {
  if (b->len + need + 1 <= b->cap) return;
  size_t ncap = b->cap;
  while (b->len + need + 1 > ncap) ncap *= 2;
  char *nb = (char *)luke_arena_alloc(b->a, ncap, 1);
  memcpy(nb, b->buf, b->len + 1);
  b->buf = nb;
  b->cap = ncap;
}

static inline void luke_json_buf_put(LukeJsonBuf *b, const char *s, size_t n) {
  luke_json_buf_grow(b, n);
  memcpy(b->buf + b->len, s, n);
  b->len += n;
  b->buf[b->len] = '\0';
}

static inline void luke_json_buf_puts(LukeJsonBuf *b, const char *s) {
  luke_json_buf_put(b, s, strlen(s));
}

static inline void luke_json_buf_putc(LukeJsonBuf *b, char c) {
  luke_json_buf_grow(b, 1);
  b->buf[b->len++] = c;
  b->buf[b->len] = '\0';
}

static inline void luke_json_stringify_into(LukeJsonBuf *b, LukeJson *v);

static inline void luke_json_escape_into(LukeJsonBuf *b, LukeText t) {
  luke_json_buf_putc(b, '"');
  for (size_t i = 0; i < t.len; ++i) {
    char c = t.ptr[i];
    if (c == '"' || c == '\\') {
      luke_json_buf_putc(b, '\\');
      luke_json_buf_putc(b, c);
    } else if (c == '\n') {
      luke_json_buf_puts(b, "\\n");
    } else if (c == '\t') {
      luke_json_buf_puts(b, "\\t");
    } else if (c == '\r') {
      luke_json_buf_puts(b, "\\r");
    } else {
      luke_json_buf_putc(b, c);
    }
  }
  luke_json_buf_putc(b, '"');
}

static inline void luke_json_stringify_into(LukeJsonBuf *b, LukeJson *v) {
  if (!v) {
    luke_json_buf_puts(b, "null");
    return;
  }
  switch (v->kind) {
    case LUKE_JSON_NULL:
      luke_json_buf_puts(b, "null");
      break;
    case LUKE_JSON_BOOL:
      luke_json_buf_puts(b, v->boolean ? "true" : "false");
      break;
    case LUKE_JSON_NUMBER: {
      char tmp[64];
      snprintf(tmp, sizeof(tmp), "%.10g", v->number);
      luke_json_buf_puts(b, tmp);
      break;
    }
    case LUKE_JSON_STRING:
      luke_json_escape_into(b, v->string);
      break;
    case LUKE_JSON_ARRAY:
      luke_json_buf_putc(b, '[');
      for (size_t i = 0; i < v->len; ++i) {
        if (i) luke_json_buf_putc(b, ',');
        luke_json_stringify_into(b, v->items[i]);
      }
      luke_json_buf_putc(b, ']');
      break;
    case LUKE_JSON_OBJECT:
      luke_json_buf_putc(b, '{');
      for (size_t i = 0; i < v->len; ++i) {
        if (i) luke_json_buf_putc(b, ',');
        luke_json_escape_into(b, v->keys[i]);
        luke_json_buf_putc(b, ':');
        luke_json_stringify_into(b, v->items[i]);
      }
      luke_json_buf_putc(b, '}');
      break;
  }
}

static inline LukeText luke_json_stringify(LukeArena *a, LukeJson *v) {
  LukeJsonBuf b;
  luke_json_buf_init(&b, a);
  luke_json_stringify_into(&b, v);
  return luke_text_n(b.buf, b.len);
}

/* Legacy helper: find "key":"value" string field in raw TEXT JSON. */
static inline LukeText luke_json_string(LukeArena *a, LukeText json, LukeText key) {
  LukeJson *root = luke_json_parse(a, json);
  LukeJson *v = luke_json_get(root, key);
  if (!v || v->kind != LUKE_JSON_STRING) return luke_text("");
  return v->string;
}

/* ---------- HTTP GET (native via curl; empty on WASI) ---------- */

static inline LukeText luke_http_get(LukeArena *a, LukeText url) {
#if defined(__wasi__)
  (void)a;
  (void)url;
  return luke_text("");
#else
  char u[2048];
  size_t n = url.len < sizeof(u) - 1 ? url.len : sizeof(u) - 1;
  memcpy(u, url.ptr, n);
  u[n] = '\0';
  /* Use curl when available — handles https. Quiet, follow redirects, fail on HTTP errors. */
  char cmd[2200];
  /* Basic sanitize: reject shell metacharacters in URL */
  for (size_t i = 0; i < n; ++i) {
    char c = u[i];
    if (c == '"' || c == '`' || c == '$' || c == ';' || c == '|' || c == '&' || c == '\n' ||
        c == '\r' || c == ' ') {
      return luke_text("");
    }
  }
  snprintf(cmd, sizeof(cmd), "curl -fsSL --max-time 20 \"%s\" 2>/dev/null", u);
  FILE *p = popen(cmd, "r");
  if (!p) return luke_text("");
  size_t cap = 4096, len = 0;
  char *buf = (char *)malloc(cap);
  if (!buf) {
    pclose(p);
    return luke_text("");
  }
  for (;;) {
    if (len + 1024 > cap) {
      cap *= 2;
      char *nb = (char *)realloc(buf, cap);
      if (!nb) {
        free(buf);
        pclose(p);
        return luke_text("");
      }
      buf = nb;
    }
    size_t got = fread(buf + len, 1, 1024, p);
    len += got;
    if (got < 1024) break;
  }
  int rc = pclose(p);
  if (rc != 0) {
    free(buf);
    return luke_text("");
  }
  char *out = (char *)luke_arena_alloc(a, len + 1, 1);
  memcpy(out, buf, len);
  out[len] = '\0';
  free(buf);
  return luke_text_n(out, len);
#endif
}

/* ---------- args / env / paths / process (tooling beachhead) ---------- */

static inline LukeText luke_get_arg(LukeArena *a, double index) {
  int i = (int)index;
  if (i < 0 || i >= luke_rt_argc || !luke_rt_argv) return luke_text("");
  (void)a;
  return luke_text(luke_rt_argv[i]);
}

static inline LukeText luke_get_env(LukeArena *a, LukeText key) {
  char name[512];
  size_t n = key.len < sizeof(name) - 1 ? key.len : sizeof(name) - 1;
  memcpy(name, key.ptr, n);
  name[n] = '\0';
#if defined(__wasi__)
  (void)a;
  (void)name;
  return luke_text("");
#else
  const char *v = getenv(name);
  if (!v) return luke_text("");
  size_t len = strlen(v);
  char *p = (char *)luke_arena_alloc(a, len + 1, 1);
  memcpy(p, v, len + 1);
  return luke_text_n(p, len);
#endif
}

static inline int luke_set_env(LukeText key, LukeText value) {
  char name[512], val[2048];
  size_t n = key.len < sizeof(name) - 1 ? key.len : sizeof(name) - 1;
  size_t m = value.len < sizeof(val) - 1 ? value.len : sizeof(val) - 1;
  memcpy(name, key.ptr, n);
  name[n] = '\0';
  memcpy(val, value.ptr, m);
  val[m] = '\0';
#if defined(__wasi__)
  (void)name;
  (void)val;
  return 0;
#else
  return setenv(name, val, 1) == 0 ? 1 : 0;
#endif
}

static inline LukeText luke_cwd(LukeArena *a) {
#if defined(__wasi__)
  (void)a;
  return luke_text(".");
#else
  char buf[4096];
  if (!getcwd(buf, sizeof(buf))) return luke_text("");
  size_t len = strlen(buf);
  char *p = (char *)luke_arena_alloc(a, len + 1, 1);
  memcpy(p, buf, len + 1);
  return luke_text_n(p, len);
#endif
}

static inline LukeText luke_path_join(LukeArena *a, LukeText left, LukeText right) {
  int needSlash = 1;
  if (left.len == 0) return right;
  if (right.len == 0) return left;
  if (left.ptr[left.len - 1] == '/' || (right.len > 0 && right.ptr[0] == '/')) needSlash = 0;
  size_t n = left.len + right.len + (needSlash ? 1 : 0);
  char *p = (char *)luke_arena_alloc(a, n + 1, 1);
  memcpy(p, left.ptr, left.len);
  size_t o = left.len;
  if (needSlash) p[o++] = '/';
  memcpy(p + o, right.ptr, right.len);
  p[n] = '\0';
  return luke_text_n(p, n);
}

static inline LukeText luke_path_basename(LukeArena *a, LukeText path) {
  (void)a;
  if (path.len == 0) return luke_text("");
  size_t i = path.len;
  while (i > 0 && path.ptr[i - 1] != '/' && path.ptr[i - 1] != '\\') --i;
  return luke_text_n(path.ptr + i, path.len - i);
}

static inline LukeText luke_path_dirname(LukeArena *a, LukeText path) {
  (void)a;
  if (path.len == 0) return luke_text(".");
  size_t i = path.len;
  while (i > 0 && path.ptr[i - 1] != '/' && path.ptr[i - 1] != '\\') --i;
  if (i == 0) return luke_text(".");
  if (i == 1) return luke_text_n(path.ptr, 1);
  return luke_text_n(path.ptr, i - 1);
}

static inline LukeText luke_shell(LukeArena *a, LukeText command) {
#if defined(__wasi__)
  (void)a;
  (void)command;
  return luke_text("");
#else
  char cmd[4096];
  size_t n = command.len < sizeof(cmd) - 1 ? command.len : sizeof(cmd) - 1;
  memcpy(cmd, command.ptr, n);
  cmd[n] = '\0';
  FILE *p = popen(cmd, "r");
  if (!p) return luke_text("");
  size_t cap = 4096, len = 0;
  char *buf = (char *)malloc(cap);
  if (!buf) {
    pclose(p);
    return luke_text("");
  }
  for (;;) {
    if (len + 1024 > cap) {
      cap *= 2;
      char *nb = (char *)realloc(buf, cap);
      if (!nb) {
        free(buf);
        pclose(p);
        return luke_text("");
      }
      buf = nb;
    }
    size_t got = fread(buf + len, 1, 1024, p);
    len += got;
    if (got < 1024) break;
  }
  pclose(p);
  char *out = (char *)luke_arena_alloc(a, len + 1, 1);
  memcpy(out, buf, len);
  out[len] = '\0';
  free(buf);
  return luke_text_n(out, len);
#endif
}

static inline int luke_exit_code(double code) {
  exit((int)code);
  return 0;
}

/* ---------- browser JS bridge (imported when LUKE_BROWSER) ---------- */

#if defined(LUKE_BROWSER)
__attribute__((import_module("lukejs"), import_name("set_text"))) void
luke_js_set_text_raw(const char *id, size_t id_len, const char *text, size_t text_len);

__attribute__((import_module("lukejs"), import_name("set_html"))) void
luke_js_set_html_raw(const char *id, size_t id_len, const char *html, size_t html_len);

__attribute__((import_module("lukejs"), import_name("get_value"))) void
luke_js_get_value_raw(const char *id, size_t id_len, char *out, size_t out_cap, size_t *out_len);

static inline int luke_js_set_text(LukeText id, LukeText text) {
  luke_js_set_text_raw(id.ptr, id.len, text.ptr, text.len);
  return 1;
}

static inline int luke_js_set_html(LukeText id, LukeText html) {
  luke_js_set_html_raw(id.ptr, id.len, html.ptr, html.len);
  return 1;
}

static inline LukeText luke_js_get_value(LukeArena *a, LukeText id) {
  char tmp[4096];
  size_t n = 0;
  luke_js_get_value_raw(id.ptr, id.len, tmp, sizeof(tmp), &n);
  if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
  char *p = (char *)luke_arena_alloc(a, n + 1, 1);
  memcpy(p, tmp, n);
  p[n] = '\0';
  return luke_text_n(p, n);
}

__attribute__((import_module("lukejs"), import_name("fetch"))) void
luke_js_fetch_raw(const char *url, size_t url_len, char *out, size_t out_cap, size_t *out_len);

__attribute__((import_module("lukejs"), import_name("on_click"))) void
luke_js_on_click_raw(const char *id, size_t id_len, const char *target, size_t target_len,
                      const char *message, size_t message_len);

static inline LukeText luke_js_fetch(LukeArena *a, LukeText url) {
  char tmp[65536];
  size_t n = 0;
  luke_js_fetch_raw(url.ptr, url.len, tmp, sizeof(tmp), &n);
  if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
  char *p = (char *)luke_arena_alloc(a, n + 1, 1);
  memcpy(p, tmp, n);
  p[n] = '\0';
  return luke_text_n(p, n);
}

static inline int luke_js_on_click(LukeText id, LukeText target, LukeText message) {
  luke_js_on_click_raw(id.ptr, id.len, target.ptr, target.len, message.ptr, message.len);
  return 1;
}

__attribute__((import_module("lukejs"), import_name("add_style"))) void
luke_js_add_style_raw(const char *css, size_t css_len);

__attribute__((import_module("lukejs"), import_name("load_font"))) void
luke_js_load_font_raw(const char *href, size_t href_len);

__attribute__((import_module("lukejs"), import_name("set_title"))) void
luke_js_set_title_raw(const char *title, size_t title_len);

static inline int luke_js_add_style(LukeText css) {
  luke_js_add_style_raw(css.ptr, css.len);
  return 1;
}

static inline int luke_js_load_font(LukeText href) {
  luke_js_load_font_raw(href.ptr, href.len);
  return 1;
}

static inline int luke_js_set_title(LukeText title) {
  luke_js_set_title_raw(title.ptr, title.len);
  return 1;
}
#else
static inline int luke_js_set_text(LukeText id, LukeText text) {
  (void)id;
  (void)text;
  return 0;
}
static inline int luke_js_set_html(LukeText id, LukeText html) {
  (void)id;
  (void)html;
  return 0;
}
static inline LukeText luke_js_get_value(LukeArena *a, LukeText id) {
  (void)a;
  (void)id;
  return luke_text("");
}
static inline LukeText luke_js_fetch(LukeArena *a, LukeText url) {
  (void)a;
  (void)url;
  return luke_text("");
}
static inline int luke_js_on_click(LukeText id, LukeText target, LukeText message) {
  (void)id;
  (void)target;
  (void)message;
  return 0;
}
static inline int luke_js_add_style(LukeText css) {
  (void)css;
  return 0;
}
static inline int luke_js_load_font(LukeText href) {
  (void)href;
  return 0;
}
static inline int luke_js_set_title(LukeText title) {
  (void)title;
  return 0;
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* LUKE_STD_H */
