#ifndef LUKE_STD_H
#define LUKE_STD_H

/* Thin Build-mode standard library helpers (no GC).
 * Linked by including this header after luke_rt.h in generated C. */

#include "luke_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/* Very small JSON string extractor: finds "key":"value" and returns value text.
 * Not a full parser — enough for demos and config snippets. */
static inline LukeText luke_json_string(LukeArena *a, LukeText json, LukeText key) {
  (void)a;
  /* Build needle: "key" */
  char needle[256];
  if (key.len + 3 >= sizeof(needle)) return luke_text("");
  needle[0] = '"';
  memcpy(needle + 1, key.ptr, key.len);
  needle[key.len + 1] = '"';
  needle[key.len + 2] = '\0';

  /* naive search in json */
  size_t i = 0;
  while (i + key.len + 2 < json.len) {
    if (json.ptr[i] == '"' && i + 1 + key.len < json.len &&
        memcmp(json.ptr + i + 1, key.ptr, key.len) == 0 &&
        json.ptr[i + 1 + key.len] == '"') {
      size_t j = i + 2 + key.len;
      while (j < json.len && (json.ptr[j] == ' ' || json.ptr[j] == '\t' || json.ptr[j] == '\n' ||
                              json.ptr[j] == '\r'))
        ++j;
      if (j < json.len && json.ptr[j] == ':') {
        ++j;
        while (j < json.len && (json.ptr[j] == ' ' || json.ptr[j] == '\t')) ++j;
        if (j < json.len && json.ptr[j] == '"') {
          ++j;
          size_t start = j;
          while (j < json.len && json.ptr[j] != '"') {
            if (json.ptr[j] == '\\' && j + 1 < json.len) j += 2;
            else ++j;
          }
          return luke_text_n(json.ptr + start, j > start ? j - start : 0);
        }
      }
    }
    ++i;
  }
  return luke_text("");
}

#ifdef __cplusplus
}
#endif

#endif /* LUKE_STD_H */
