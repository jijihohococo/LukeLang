#ifndef LUKE_DB_H
#define LUKE_DB_H

/* Thin SQLite wrapper for Luke Build mode.
 *
 * The driver is compiled in only when the program imports std/sqlite — the
 * build defines LUKE_HAVE_SQLITE alongside -lsqlite3. Every other program
 * gets the same no-op stubs WASI uses, so hello-world builds on a machine
 * with no SQLite headers installed. */

#include "luke_rt.h"

#if !defined(__wasi__) && defined(LUKE_HAVE_SQLITE)
#define LUKE_DB_REAL 1
#include <sqlite3.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LukeDb LukeDb;

#if !defined(LUKE_DB_REAL)

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

#else /* LUKE_DB_REAL */

#include <stdlib.h>
#include <string.h>

#ifndef LUKE_DB_STMT_CACHE
#define LUKE_DB_STMT_CACHE 128
#endif
#ifndef LUKE_DB_MMAP_SIZE
#define LUKE_DB_MMAP_SIZE (256LL * 1024 * 1024)
#endif
#ifndef LUKE_DB_CACHE_KIB
#define LUKE_DB_CACHE_KIB 65536 /* negative PRAGMA cache_size = KiB */
#endif

typedef struct LukeDbStmt {
  char *sql;
  size_t sql_len;
  sqlite3_stmt *stmt;
  struct LukeDbStmt *next;
} LukeDbStmt;

struct LukeDb {
  sqlite3 *db;
  char *path;
  size_t path_len;
  int refs;
  int pooled;
  int stmt_n;
  LukeDbStmt *stmts;
  struct LukeDb *next;
};

static __thread LukeDb *luke_db__tls = NULL;
static __thread char *luke_db__qbuf = NULL;
static __thread size_t luke_db__qcap = 0;

static inline int luke_db__pool_enabled(void) {
  const char *e = getenv("LUKE_DB_POOL");
  if (e && e[0] == '0') return 0;
  return 1;
}

static inline void luke_db__cstr(LukeText t, char *out, size_t cap) {
  size_t n = t.len < cap - 1 ? t.len : cap - 1;
  if (n) memcpy(out, t.ptr, n);
  out[n] = '\0';
}

static inline void luke_db__exec_pragma(sqlite3 *raw, const char *sql) {
  char *errmsg = NULL;
  (void)sqlite3_exec(raw, sql, NULL, NULL, &errmsg);
  if (errmsg) sqlite3_free(errmsg);
}

static inline void luke_db__apply_pragmas(sqlite3 *raw) {
  luke_db__exec_pragma(raw, "PRAGMA journal_mode=WAL;");
  luke_db__exec_pragma(raw, "PRAGMA busy_timeout=5000;");
  luke_db__exec_pragma(raw, "PRAGMA synchronous=NORMAL;");
  {
    char sql[96];
    snprintf(sql, sizeof(sql), "PRAGMA mmap_size=%lld;", (long long)LUKE_DB_MMAP_SIZE);
    luke_db__exec_pragma(raw, sql);
  }
  {
    char sql[64];
    snprintf(sql, sizeof(sql), "PRAGMA cache_size=-%d;", (int)LUKE_DB_CACHE_KIB);
    luke_db__exec_pragma(raw, sql);
  }
}

static inline LukeDb *luke_db__tls_find(const char *path, size_t path_len) {
  for (LukeDb *d = luke_db__tls; d; d = d->next) {
    if (d->path_len == path_len && d->path && memcmp(d->path, path, path_len) == 0) return d;
  }
  return NULL;
}

static inline void luke_db__stmt_cache_clear(LukeDb *db) {
  LukeDbStmt *s = db->stmts;
  while (s) {
    LukeDbStmt *n = s->next;
    if (s->stmt) sqlite3_finalize(s->stmt);
    free(s->sql);
    free(s);
    s = n;
  }
  db->stmts = NULL;
  db->stmt_n = 0;
}

static inline void luke_db__destroy(LukeDb *db) {
  if (!db) return;
  luke_db__stmt_cache_clear(db);
  if (db->db) {
    sqlite3_close(db->db);
    db->db = NULL;
  }
  free(db->path);
  free(db);
}

static inline sqlite3_stmt *luke_db__stmt_acquire(LukeDb *db, const char *sql, size_t sql_len) {
  if (!db || !db->db || !sql) return NULL;
  for (LukeDbStmt *s = db->stmts; s; s = s->next) {
    if (s->sql_len == sql_len && memcmp(s->sql, sql, sql_len) == 0) {
      sqlite3_reset(s->stmt);
      sqlite3_clear_bindings(s->stmt);
      return s->stmt;
    }
  }
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db->db, sql, (int)sql_len, &stmt, NULL) != SQLITE_OK) return NULL;
  if (db->stmt_n >= LUKE_DB_STMT_CACHE) return stmt; /* uncached — caller must finalize */
  LukeDbStmt *node = (LukeDbStmt *)calloc(1, sizeof(LukeDbStmt));
  if (!node) return stmt;
  node->sql = (char *)malloc(sql_len + 1);
  if (!node->sql) {
    free(node);
    return stmt;
  }
  memcpy(node->sql, sql, sql_len);
  node->sql[sql_len] = '\0';
  node->sql_len = sql_len;
  node->stmt = stmt;
  node->next = db->stmts;
  db->stmts = node;
  db->stmt_n++;
  return stmt;
}

static inline int luke_db__stmt_is_cached(LukeDb *db, sqlite3_stmt *stmt) {
  if (!db || !stmt) return 0;
  for (LukeDbStmt *s = db->stmts; s; s = s->next)
    if (s->stmt == stmt) return 1;
  return 0;
}

static inline void luke_db__stmt_release(LukeDb *db, sqlite3_stmt *stmt) {
  if (!stmt) return;
  if (luke_db__stmt_is_cached(db, stmt)) {
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    return;
  }
  sqlite3_finalize(stmt);
}

static inline LukeDb *luke_db_open(LukeArena *a, LukeText path) {
  (void)a;
  char name[1024];
  luke_db__cstr(path, name, sizeof(name));
  size_t path_len = strlen(name);

  if (luke_db__pool_enabled()) {
    LukeDb *hit = luke_db__tls_find(name, path_len);
    if (hit && hit->db) {
      hit->refs++;
      return hit;
    }
  }

  sqlite3 *raw = NULL;
  int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
#ifdef SQLITE_OPEN_NOMUTEX
  flags |= SQLITE_OPEN_NOMUTEX;
#endif
  if (sqlite3_open_v2(name, &raw, flags, NULL) != SQLITE_OK) {
    if (raw) sqlite3_close(raw);
    return NULL;
  }
  luke_db__apply_pragmas(raw);

  LukeDb *db = (LukeDb *)calloc(1, sizeof(LukeDb));
  if (!db) {
    sqlite3_close(raw);
    return NULL;
  }
  db->db = raw;
  db->path = (char *)malloc(path_len + 1);
  if (!db->path) {
    luke_db__destroy(db);
    return NULL;
  }
  memcpy(db->path, name, path_len + 1);
  db->path_len = path_len;
  db->refs = 1;
  db->pooled = luke_db__pool_enabled();
  if (db->pooled) {
    db->next = luke_db__tls;
    luke_db__tls = db;
  }
  return db;
}

static inline int luke_db_close(LukeDb *db) {
  if (!db) return 0;
  if (db->pooled) {
    if (db->refs > 0) db->refs--;
    /* Stay open in the thread-local pool — sqlite3_close only on real destroy. */
    return 1;
  }
  /* LUKE_DB_POOL=0 — real close. */
  if (luke_db__tls == db)
    luke_db__tls = db->next;
  else {
    for (LukeDb *p = luke_db__tls; p; p = p->next) {
      if (p->next == db) {
        p->next = db->next;
        break;
      }
    }
  }
  luke_db__destroy(db);
  return 1;
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

/* Bind LIST-of-TEXT params. Arena/request lifetime covers the step → SQLITE_STATIC. */
static inline int luke_db__bind_list(sqlite3_stmt *stmt, LukeList *params) {
  if (!stmt) return 0;
  int n = params ? (int)params->len : 0;
  for (int i = 0; i < n; ++i) {
    LukeText v = params->items[i];
    if (sqlite3_bind_text(stmt, i + 1, v.ptr ? v.ptr : "", (int)v.len, SQLITE_STATIC) != SQLITE_OK)
      return 0;
  }
  return 1;
}

static inline int luke_db_exec_bind(LukeDb *db, LukeText sql, LukeList *params) {
  if (!db || !db->db) return 0;
  char *q = (char *)malloc(sql.len + 1);
  if (!q) return 0;
  if (sql.len) memcpy(q, sql.ptr, sql.len);
  q[sql.len] = '\0';
  sqlite3_stmt *stmt = luke_db__stmt_acquire(db, q, sql.len);
  free(q);
  if (!stmt) return 0;
  if (!luke_db__bind_list(stmt, params)) {
    luke_db__stmt_release(db, stmt);
    return 0;
  }
  int rc = sqlite3_step(stmt);
  luke_db__stmt_release(db, stmt);
  return rc == SQLITE_DONE || rc == SQLITE_ROW ? 1 : 0;
}

static inline int luke_db__qbuf_grow(size_t need) {
  if (need <= luke_db__qcap) return 1;
  size_t cap = luke_db__qcap ? luke_db__qcap : 256;
  while (cap < need) cap *= 2;
  char *nb = (char *)realloc(luke_db__qbuf, cap);
  if (!nb) return 0;
  luke_db__qbuf = nb;
  luke_db__qcap = cap;
  return 1;
}

/* Parameterized SELECT — concatenate first-column values separated by newlines. */
static inline LukeText luke_db_query_bind_text(LukeArena *a, LukeDb *db, LukeText sql,
                                              LukeList *params) {
  if (!db || !db->db) return luke_text("");
  char *q = (char *)malloc(sql.len + 1);
  if (!q) return luke_text("");
  if (sql.len) memcpy(q, sql.ptr, sql.len);
  q[sql.len] = '\0';

  sqlite3_stmt *stmt = luke_db__stmt_acquire(db, q, sql.len);
  free(q);
  if (!stmt) return luke_text("");
  if (!luke_db__bind_list(stmt, params)) {
    luke_db__stmt_release(db, stmt);
    return luke_text("");
  }

  size_t len = 0;
  int first = 1;
  if (!luke_db__qbuf_grow(256)) {
    luke_db__stmt_release(db, stmt);
    return luke_text("");
  }
  luke_db__qbuf[0] = '\0';

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char *text = sqlite3_column_text(stmt, 0);
    const char *s = text ? (const char *)text : "";
    size_t slen = (size_t)sqlite3_column_bytes(stmt, 0);
    size_t need = len + (first ? 0 : 1) + slen + 1;
    if (!luke_db__qbuf_grow(need)) {
      luke_db__stmt_release(db, stmt);
      return luke_text("");
    }
    if (!first) luke_db__qbuf[len++] = '\n';
    memcpy(luke_db__qbuf + len, s, slen);
    len += slen;
    luke_db__qbuf[len] = '\0';
    first = 0;
  }

  luke_db__stmt_release(db, stmt);

  char *out = (char *)luke_arena_alloc(a, len + 1, 1);
  if (len) memcpy(out, luke_db__qbuf, len);
  out[len] = '\0';
  return luke_text_n(out, len);
}

static inline LukeText luke_db_query_text(LukeArena *a, LukeDb *db, LukeText sql) {
  return luke_db_query_bind_text(a, db, sql, NULL);
}

static inline int64_t luke_db_data_version(LukeDb *db) {
  if (!db || !db->db) return 0;
  static const char sql[] = "PRAGMA data_version";
  sqlite3_stmt *stmt = luke_db__stmt_acquire(db, sql, sizeof(sql) - 1);
  if (!stmt) return 0;
  int64_t v = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) v = sqlite3_column_int64(stmt, 0);
  luke_db__stmt_release(db, stmt);
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
  sqlite3_stmt *stmt = luke_db__stmt_acquire(db, q, sql.len);
  free(q);
  if (!stmt) return 0;
  int64_t v = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) v = sqlite3_column_int64(stmt, 0);
  luke_db__stmt_release(db, stmt);
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
  int slen = snprintf(sql, sizeof(sql), "INSERT INTO %s(v) VALUES(?)", tname);
  if (slen < 0 || (size_t)slen >= sizeof(sql)) return 0;
  sqlite3_stmt *stmt = luke_db__stmt_acquire(db, sql, (size_t)slen);
  if (!stmt) return 0;
  sqlite3_bind_text(stmt, 1, value.ptr ? value.ptr : "", (int)value.len, SQLITE_STATIC);
  int rc = sqlite3_step(stmt);
  luke_db__stmt_release(db, stmt);
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
  int slen = snprintf(sql, sizeof(sql), "SELECT seq, v FROM %s WHERE seq > %lld ORDER BY seq", tname,
                      (long long)after_seq);
  if (slen < 0 || (size_t)slen >= sizeof(sql)) return 0;
  /* Dynamic after_seq → do not cache (would bind wrong on reuse). */
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db->db, sql, slen, &stmt, NULL) != SQLITE_OK) return 0;
  int count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    int64_t seq = sqlite3_column_int64(stmt, 0);
    const unsigned char *tv = sqlite3_column_text(stmt, 1);
    const char *ts = tv ? (const char *)tv : "";
    size_t len = (size_t)sqlite3_column_bytes(stmt, 1);
    char *p = (char *)luke_arena_alloc(a, len + 1, 1);
    if (len) memcpy(p, ts, len);
    p[len] = '\0';
    fn(ctx, seq, luke_text_n(p, len));
    count++;
  }
  sqlite3_finalize(stmt);
  return count;
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
