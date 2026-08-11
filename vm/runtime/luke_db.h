#ifndef LUKE_DB_H
#define LUKE_DB_H

/* Thin SQLite wrapper for Luke Build mode (native only; stubbed on WASI). */

#include "luke_rt.h"

#if !defined(__wasi__)
#include <sqlite3.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LukeDb LukeDb;

#if defined(__wasi__)

struct LukeDb {
  int unused;
};

static inline LukeDb *luke_db_open(LukeArena *a, LukeText path) {
  (void)a;
  (void)path;
  return NULL;
}

static inline int luke_db_exec(LukeDb *db, LukeText sql) {
  (void)db;
  (void)sql;
  return 0;
}

static inline LukeText luke_db_query_text(LukeArena *a, LukeDb *db, LukeText sql) {
  (void)a;
  (void)db;
  (void)sql;
  return luke_text("");
}

static inline int luke_db_close(LukeDb *db) {
  (void)db;
  return 0;
}

static inline int64_t luke_db_data_version(LukeDb *db) {
  (void)db;
  return 0;
}

#else /* !__wasi__ */

struct LukeDb {
  sqlite3 *db;
};

static inline void luke_db__cstr(LukeText t, char *out, size_t cap) {
  size_t n = t.len < cap - 1 ? t.len : cap - 1;
  if (n) memcpy(out, t.ptr, n);
  out[n] = '\0';
}

static inline LukeDb *luke_db_open(LukeArena *a, LukeText path) {
  char name[1024];
  luke_db__cstr(path, name, sizeof(name));
  sqlite3 *raw = NULL;
  if (sqlite3_open(name, &raw) != SQLITE_OK) {
    if (raw) sqlite3_close(raw);
    return NULL;
  }
  LukeDb *db = (LukeDb *)luke_arena_alloc(a, sizeof(LukeDb), 8);
  db->db = raw;
  return db;
}

static inline int luke_db_exec(LukeDb *db, LukeText sql) {
  if (!db || !db->db) return 0;
  char *q = (char *)malloc(sql.len + 1);
  if (!q) return 0;
  if (sql.len) memcpy(q, sql.ptr, sql.len);
  q[sql.len] = '\0';
  char *errmsg = NULL;
  int rc = sqlite3_exec(db->db, q, NULL, NULL, &errmsg);
  free(q);
  if (errmsg) sqlite3_free(errmsg);
  return rc == SQLITE_OK ? 1 : 0;
}

/* SELECT: concatenate first-column values separated by newlines. */
static inline LukeText luke_db_query_text(LukeArena *a, LukeDb *db, LukeText sql) {
  if (!db || !db->db) return luke_text("");
  char *q = (char *)malloc(sql.len + 1);
  if (!q) return luke_text("");
  if (sql.len) memcpy(q, sql.ptr, sql.len);
  q[sql.len] = '\0';

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db->db, q, -1, &stmt, NULL) != SQLITE_OK) {
    free(q);
    return luke_text("");
  }
  free(q);

  size_t cap = 256, len = 0;
  char *buf = (char *)malloc(cap);
  if (!buf) {
    sqlite3_finalize(stmt);
    return luke_text("");
  }
  buf[0] = '\0';
  int first = 1;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char *text = sqlite3_column_text(stmt, 0);
    const char *s = text ? (const char *)text : "";
    size_t slen = strlen(s);
    size_t need = len + (first ? 0 : 1) + slen + 1;
    if (need > cap) {
      while (cap < need) cap *= 2;
      char *nb = (char *)realloc(buf, cap);
      if (!nb) {
        free(buf);
        sqlite3_finalize(stmt);
        return luke_text("");
      }
      buf = nb;
    }
    if (!first) buf[len++] = '\n';
    memcpy(buf + len, s, slen);
    len += slen;
    buf[len] = '\0';
    first = 0;
  }

  sqlite3_finalize(stmt);

  char *out = (char *)luke_arena_alloc(a, len + 1, 1);
  if (len) memcpy(out, buf, len);
  out[len] = '\0';
  free(buf);
  return luke_text_n(out, len);
}

/* Cross-connection change detector — increments when any commit modifies the DB file. */
static inline int64_t luke_db_data_version(LukeDb *db) {
  if (!db || !db->db) return 0;
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db->db, "PRAGMA data_version", -1, &stmt, NULL) != SQLITE_OK) return 0;
  int64_t v = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) v = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return v;
}

static inline int luke_db_close(LukeDb *db) {
  if (!db || !db->db) return 0;
  sqlite3_close(db->db);
  db->db = NULL;
  return 1;
}

#endif /* !__wasi__ */

#ifdef __cplusplus
}
#endif

#endif /* LUKE_DB_H */
