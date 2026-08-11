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

static inline int luke_db_exec_bind(LukeDb *db, LukeText sql, LukeList *params) {
  (void)db;
  (void)sql;
  (void)params;
  return 0;
}

static inline LukeText luke_db_query_text(LukeArena *a, LukeDb *db, LukeText sql) {
  (void)a;
  (void)db;
  (void)sql;
  return luke_text("");
}

static inline LukeText luke_db_query_bind_text(LukeArena *a, LukeDb *db, LukeText sql, LukeList *params) {
  (void)a;
  (void)db;
  (void)sql;
  (void)params;
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

static inline int64_t luke_db_last_insert_rowid(LukeDb *db) {
  (void)db;
  return 0;
}

static inline int64_t luke_db_query_i64(LukeDb *db, LukeText sql) {
  (void)db;
  (void)sql;
  return 0;
}

static inline int64_t luke_db_log_append(LukeDb *db, LukeText table, LukeText value) {
  (void)db;
  (void)table;
  (void)value;
  return 0;
}

static inline int luke_db_migrate_ensure(LukeDb *db) {
  (void)db;
  return 0;
}

static inline int64_t luke_db_migrate_version(LukeDb *db) {
  (void)db;
  return 0;
}

static inline int luke_db_migrate_set(LukeDb *db, int64_t version) {
  (void)db;
  (void)version;
  return 0;
}

static inline int luke_db_log_replay(LukeArena *a, LukeDb *db, LukeText table, int64_t after_seq,
                                     void (*fn)(void *, int64_t, LukeText), void *ctx) {
  (void)a;
  (void)db;
  (void)table;
  (void)after_seq;
  (void)fn;
  (void)ctx;
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

/* Bind LIST-of-TEXT params as SQLITE TEXT (?1 …). Returns 1 on success. */
static inline int luke_db__bind_list(sqlite3_stmt *stmt, LukeList *params) {
  if (!stmt) return 0;
  int n = params ? (int)params->len : 0;
  for (int i = 0; i < n; ++i) {
    LukeText v = params->items[i];
    if (sqlite3_bind_text(stmt, i + 1, v.ptr ? v.ptr : "", (int)v.len, SQLITE_TRANSIENT) !=
        SQLITE_OK)
      return 0;
  }
  return 1;
}

/* Parameterized exec — use for any SQL that includes user/app values. */
static inline int luke_db_exec_bind(LukeDb *db, LukeText sql, LukeList *params) {
  if (!db || !db->db) return 0;
  char *q = (char *)malloc(sql.len + 1);
  if (!q) return 0;
  if (sql.len) memcpy(q, sql.ptr, sql.len);
  q[sql.len] = '\0';
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db->db, q, -1, &stmt, NULL) != SQLITE_OK) {
    free(q);
    return 0;
  }
  free(q);
  if (!luke_db__bind_list(stmt, params)) {
    sqlite3_finalize(stmt);
    return 0;
  }
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE || rc == SQLITE_ROW ? 1 : 0;
}

/* Parameterized SELECT — concatenate first-column values separated by newlines. */
static inline LukeText luke_db_query_bind_text(LukeArena *a, LukeDb *db, LukeText sql,
                                              LukeList *params) {
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
  if (!luke_db__bind_list(stmt, params)) {
    sqlite3_finalize(stmt);
    return luke_text("");
  }

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

/* Unparameterized SELECT — prefer luke_db_query_bind_text for user values. */
static inline LukeText luke_db_query_text(LukeArena *a, LukeDb *db, LukeText sql) {
  return luke_db_query_bind_text(a, db, sql, NULL);
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

static inline int64_t luke_db_last_insert_rowid(LukeDb *db) {
  if (!db || !db->db) return 0;
  return (int64_t)sqlite3_last_insert_rowid(db->db);
}

static inline int64_t luke_db_query_i64(LukeDb *db, LukeText sql) {
  if (!db || !db->db) return 0;
  char *q = (char *)malloc(sql.len + 1);
  if (!q) return 0;
  if (sql.len) memcpy(q, sql.ptr, sql.len);
  q[sql.len] = '\0';
  sqlite3_stmt *stmt = NULL;
  int64_t v = 0;
  if (sqlite3_prepare_v2(db->db, q, -1, &stmt, NULL) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) v = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
  }
  free(q);
  return v;
}

/* Append a TEXT row to an AUTOINCREMENT log table; returns new seq (rowid). */
static inline int64_t luke_db_log_append(LukeDb *db, LukeText table, LukeText value) {
  if (!db || !db->db || table.len == 0) return 0;
  char sql[192];
  size_t n = table.len < 120 ? table.len : 120;
  char tname[128];
  memcpy(tname, table.ptr, n);
  tname[n] = '\0';
  snprintf(sql, sizeof(sql), "INSERT INTO %s(v) VALUES(?)", tname);
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
  sqlite3_bind_text(stmt, 1, value.ptr ? value.ptr : "", (int)value.len, SQLITE_TRANSIENT);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) return 0;
  return (int64_t)sqlite3_last_insert_rowid(db->db);
}

typedef void (*LukeDbLogReplayFn)(void *ctx, int64_t seq, LukeText value);

static inline int luke_db_log_replay(LukeArena *a, LukeDb *db, LukeText table, int64_t after_seq,
                                     LukeDbLogReplayFn fn, void *ctx) {
  if (!db || !db->db || !fn || table.len == 0) return 0;
  char tname[128];
  size_t n = table.len < 120 ? table.len : 120;
  memcpy(tname, table.ptr, n);
  tname[n] = '\0';
  char sql[256];
  snprintf(sql, sizeof(sql), "SELECT seq, v FROM %s WHERE seq > %lld ORDER BY seq", tname,
           (long long)after_seq);
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
  int count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    int64_t seq = sqlite3_column_int64(stmt, 0);
    const unsigned char *tv = sqlite3_column_text(stmt, 1);
    const char *ts = tv ? (const char *)tv : "";
    size_t len = strlen(ts);
    char *p = (char *)luke_arena_alloc(a, len + 1, 1);
    if (len) memcpy(p, ts, len);
    p[len] = '\0';
    fn(ctx, seq, luke_text_n(p, len));
    count++;
  }
  sqlite3_finalize(stmt);
  return count;
}

static inline int luke_db_close(LukeDb *db) {
  if (!db || !db->db) return 0;
  sqlite3_close(db->db);
  db->db = NULL;
  return 1;
}

/* Conventional schema version table — single row id=1. */
static inline int luke_db_migrate_ensure(LukeDb *db) {
  return luke_db_exec(db, luke_text("CREATE TABLE IF NOT EXISTS luke_schema_migrations("
                                    "id INTEGER PRIMARY KEY CHECK(id=1), "
                                    "version INTEGER NOT NULL)"));
}

static inline int64_t luke_db_migrate_version(LukeDb *db) {
  if (!db || !db->db) return 0;
  return luke_db_query_i64(db, luke_text("SELECT version FROM luke_schema_migrations WHERE id=1"));
}

static inline int luke_db_migrate_set(LukeDb *db, int64_t version) {
  if (!db || !db->db) return 0;
  char sql[160];
  snprintf(sql, sizeof(sql),
           "INSERT INTO luke_schema_migrations(id, version) VALUES(1, %lld) "
           "ON CONFLICT(id) DO UPDATE SET version=excluded.version",
           (long long)version);
  return luke_db_exec(db, luke_text(sql));
}

#endif /* !__wasi__ */

#ifdef __cplusplus
}
#endif

#endif /* LUKE_DB_H */
