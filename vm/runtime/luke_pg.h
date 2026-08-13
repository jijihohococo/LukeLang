#ifndef LUKE_PG_H
#define LUKE_PG_H

/* Postgres (libpq) driver for Luke Build — Phase 1 blocking TLS pool +
 * Slipstream: adaptive sharded pipelined executor (opt-in via LUKE_PG_ASYNC=1).
 * See docs/NETWORK_DB_ROADMAP.md. */

#include "luke_rt.h"

#if !defined(__wasi__)
#include <postgresql/libpq-fe.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/epoll.h>
#include <sys/eventfd.h>
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LukePg LukePg;

#if defined(__wasi__)

struct LukePg {
  int unused;
};

static inline LukePg *luke_pg_open(LukeArena *a, LukeText conninfo) {
  (void)a;
  (void)conninfo;
  return NULL;
}
static inline int luke_pg_exec_bind(LukePg *pg, LukeText sql, LukeList *params) {
  (void)pg;
  (void)sql;
  (void)params;
  return 0;
}
static inline LukeText luke_pg_query_bind(LukeArena *a, LukePg *pg, LukeText sql, LukeList *params) {
  (void)a;
  (void)pg;
  (void)sql;
  (void)params;
  return luke_text("");
}
static inline LukeText luke_pg_rows_bind(LukeArena *a, LukePg *pg, LukeText sql, LukeList *params) {
  (void)a;
  (void)pg;
  (void)sql;
  (void)params;
  return luke_text("");
}
static inline int luke_pg_close(LukePg *pg) {
  (void)pg;
  return 0;
}
static inline int luke_pg_checkout(LukePg *pg) {
  (void)pg;
  return 0;
}
static inline int luke_pg_checkin(LukePg *pg) {
  (void)pg;
  return 0;
}

#else /* !__wasi__ */

#ifndef LUKE_PG_STMT_CACHE
#define LUKE_PG_STMT_CACHE 128
#endif
#ifndef LUKE_PG_CONNS
#define LUKE_PG_CONNS 8
#endif
#ifndef LUKE_PG_QUEUE
#define LUKE_PG_QUEUE 1024
#endif
#ifndef LUKE_PG_PIPELINE_DEPTH
#define LUKE_PG_PIPELINE_DEPTH 32
#endif
#ifndef LUKE_PG_DRAFT_MIN
#define LUKE_PG_DRAFT_MIN 2
#endif

typedef struct LukePgStmt {
  char *sql;
  size_t sql_len;
  char name[32];
  struct LukePgStmt *next;
} LukePgStmt;

struct LukePg {
  char *conninfo;
  size_t conninfo_len;
  int refs;
  int pooled;
  PGconn *blocking; /* Phase-1 TLS connection; NULL in Slipstream mode */
  PGconn *pinned;   /* checkout — transactional pin; never drafted */
  LukePgStmt *stmts;
  int stmt_n;
  struct LukePg *next;
};

/* ---------- helpers ---------- */

static inline int luke_pg__pool_enabled(void) {
  const char *e = getenv("LUKE_PG_POOL");
  return !(e && e[0] == '0');
}

/* Slipstream is opt-in until localhost parity + real-latency acceptance pass.
 * Honest default remains blocking (beats Go on localhost). */
static inline int luke_pg__async_enabled(void) {
  const char *e = getenv("LUKE_PG_ASYNC");
  return e && e[0] == '1';
}

static inline int luke_pg__nconns(void) {
  int n = LUKE_PG_CONNS;
  const char *e = getenv("LUKE_PG_CONNS");
  if (e && e[0]) n = atoi(e);
  if (n < 1) n = 1;
  if (n > 64) n = 64;
  return n;
}

static inline int luke_pg__nshards(void) {
  const char *e = getenv("LUKE_PG_SHARDS");
  if (e && e[0]) {
    int n = atoi(e);
    if (n < 1) n = 1;
    if (n > 64) n = 64;
    return n;
  }
  long c = sysconf(_SC_NPROCESSORS_ONLN);
  if (c < 1) c = 1;
  if (c > 64) c = 64;
  return (int)c;
}

static inline int luke_pg__pipeline_depth(void) {
  int n = LUKE_PG_PIPELINE_DEPTH;
  const char *e = getenv("LUKE_PG_PIPELINE_DEPTH");
  if (e && e[0]) n = atoi(e);
  if (n < 1) n = 1;
  if (n > 256) n = 256;
  return n;
}

static inline int luke_pg__draft_min(void) {
  int n = LUKE_PG_DRAFT_MIN;
  const char *e = getenv("LUKE_PG_DRAFT_MIN");
  if (e && e[0]) n = atoi(e);
  if (n < 1) n = 1;
  if (n > 64) n = 64;
  return n;
}

static inline void luke_pg__cstr(LukeText t, char *out, size_t cap) {
  size_t n = t.len < cap - 1 ? t.len : cap - 1;
  if (n && t.ptr) memcpy(out, t.ptr, n);
  out[n] = '\0';
}

static inline unsigned luke_pg__hash(const char *s, size_t n) {
  unsigned h = 2166136261u;
  for (size_t i = 0; i < n; ++i) {
    h ^= (unsigned char)s[i];
    h *= 16777619u;
  }
  return h;
}

static inline char **luke_pg__dup_params(LukeList *params, int *n_out, int **lens_out) {
  int n = params ? (int)params->len : 0;
  *n_out = n;
  if (lens_out) *lens_out = NULL;
  if (n <= 0) return NULL;
  char **vals = (char **)calloc((size_t)n, sizeof(char *));
  int *lens = (int *)calloc((size_t)n, sizeof(int));
  if (!vals || !lens) {
    free(vals);
    free(lens);
    return NULL;
  }
  for (int i = 0; i < n; ++i) {
    LukeText v = params->items[i];
    lens[i] = (int)v.len;
    vals[i] = (char *)malloc((size_t)v.len + 1);
    if (!vals[i]) {
      for (int j = 0; j < i; ++j) free(vals[j]);
      free(vals);
      free(lens);
      return NULL;
    }
    if (v.len && v.ptr) memcpy(vals[i], v.ptr, v.len);
    vals[i][v.len] = '\0';
  }
  if (lens_out) *lens_out = lens;
  else free(lens);
  return vals;
}

static inline void luke_pg__free_params(char **vals, int *lens, int n) {
  if (vals) {
    for (int i = 0; i < n; ++i) free(vals[i]);
    free(vals);
  }
  free(lens);
}

/* ---------- Phase 1: blocking TLS pool ---------- */

static __thread LukePg *luke_pg__tls = NULL;

static inline LukePg *luke_pg__tls_find(const char *ci, size_t len) {
  for (LukePg *p = luke_pg__tls; p; p = p->next) {
    if (p->conninfo_len == len && p->conninfo && memcmp(p->conninfo, ci, len) == 0) return p;
  }
  return NULL;
}

static inline void luke_pg__stmt_clear(LukePg *pg) {
  LukePgStmt *s = pg->stmts;
  while (s) {
    LukePgStmt *n = s->next;
    free(s->sql);
    free(s);
    s = n;
  }
  pg->stmts = NULL;
  pg->stmt_n = 0;
}

static inline void luke_pg__destroy(LukePg *pg) {
  if (!pg) return;
  luke_pg__stmt_clear(pg);
  if (pg->pinned) {
    PQfinish(pg->pinned);
    pg->pinned = NULL;
  }
  if (pg->blocking) {
    PQfinish(pg->blocking);
    pg->blocking = NULL;
  }
  free(pg->conninfo);
  free(pg);
}

static inline PGconn *luke_pg__active_blocking(LukePg *pg) {
  if (pg->pinned) return pg->pinned;
  return pg->blocking;
}

static inline const char *luke_pg__stmt_name(LukePg *pg, const char *sql, size_t sql_len) {
  for (LukePgStmt *s = pg->stmts; s; s = s->next) {
    if (s->sql_len == sql_len && memcmp(s->sql, sql, sql_len) == 0) return s->name;
  }
  PGconn *c = luke_pg__active_blocking(pg);
  if (!c) return NULL;
  if (pg->stmt_n >= LUKE_PG_STMT_CACHE) return NULL;
  unsigned h = luke_pg__hash(sql, sql_len);
  char name[32];
  snprintf(name, sizeof(name), "luke_p_%08x", h ^ (unsigned)pg->stmt_n);
  PGresult *res = PQprepare(c, name, sql, 0, NULL);
  if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
    if (res) PQclear(res);
    return NULL;
  }
  PQclear(res);
  LukePgStmt *node = (LukePgStmt *)calloc(1, sizeof(LukePgStmt));
  if (!node) return NULL;
  node->sql = (char *)malloc(sql_len + 1);
  if (!node->sql) {
    free(node);
    return NULL;
  }
  memcpy(node->sql, sql, sql_len);
  node->sql[sql_len] = '\0';
  node->sql_len = sql_len;
  memcpy(node->name, name, sizeof(name));
  node->next = pg->stmts;
  pg->stmts = node;
  pg->stmt_n++;
  return node->name;
}

static inline PGresult *luke_pg__exec_params(LukePg *pg, const char *sql, size_t sql_len,
                                             LukeList *params) {
  PGconn *c = luke_pg__active_blocking(pg);
  if (!c) return NULL;
  int n = 0;
  int *lens = NULL;
  char **vals = luke_pg__dup_params(params, &n, &lens);
  const char *stmt = luke_pg__stmt_name(pg, sql, sql_len);
  PGresult *res = NULL;
  if (stmt) {
    res = PQexecPrepared(c, stmt, n, (const char *const *)vals, lens, NULL, 0);
  } else {
    char *q = (char *)malloc(sql_len + 1);
    if (!q) {
      luke_pg__free_params(vals, lens, n);
      return NULL;
    }
    memcpy(q, sql, sql_len);
    q[sql_len] = '\0';
    res = PQexecParams(c, q, n, NULL, (const char *const *)vals, lens, NULL, 0);
    free(q);
  }
  luke_pg__free_params(vals, lens, n);
  return res;
}

static inline int luke_pg__result_ok(PGresult *res) {
  if (!res) return 0;
  ExecStatusType st = PQresultStatus(res);
  return st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK || st == PGRES_SINGLE_TUPLE;
}

/* ---------- Slipstream: sharded opportunistic pipelined executor ---------- */

typedef struct LukePgWaiter {
  char *sql;
  size_t sql_len;
  char **params;
  int *param_lens;
  int nparams;
  int want_rows; /* 0 = first cell only; 1 = all first-col rows */
  pthread_mutex_t mu;
  pthread_cond_t cv;
  int done;
  int ok;
  char *text; /* malloc'd result */
  struct LukePgWaiter *q_next;
  struct LukePgWaiter *fifo_next; /* in-flight on a connection */
} LukePgWaiter;

typedef struct LukePgSlotStmt {
  char *sql;
  size_t sql_len;
  char name[32];
  struct LukePgSlotStmt *next;
} LukePgSlotStmt;

typedef struct LukePgSlot {
  PGconn *conn;
  int fd;
  LukePgWaiter *fifo_head;
  LukePgWaiter *fifo_tail;
  int in_flight;
  int in_pipeline;
  int expecting_sync;
  LukePgSlotStmt *stmts;
  int stmt_n;
} LukePgSlot;

typedef struct LukePgShard {
  char *conninfo; /* borrowed from pool — not freed here */
  LukePgSlot *slots;
  int nslots;
  pthread_t thr;
  int started;
  int stop;
  int wake_fd;
  int wake_wr;
  int epfd;
  pthread_mutex_t qmu; /* per-shard; only producers contend within shard */
  LukePgWaiter *q_head;
  LukePgWaiter *q_tail;
  int q_len;
} LukePgShard;

typedef struct LukePgPool {
  char *conninfo;
  LukePgShard *shards;
  int nshards;
  struct LukePgPool *next;
} LukePgPool;

static pthread_mutex_t luke_pg__pool_mu = PTHREAD_MUTEX_INITIALIZER;
static LukePgPool *luke_pg__pools = NULL;
static atomic_int luke_pg__worker_seq = 0;
static __thread int luke_pg__worker_idx = -1;

static inline int luke_pg__worker_shard(int nshards) {
  if (nshards <= 1) return 0;
  if (luke_pg__worker_idx < 0)
    luke_pg__worker_idx = atomic_fetch_add_explicit(&luke_pg__worker_seq, 1, memory_order_relaxed);
  if (luke_pg__worker_idx < 0) luke_pg__worker_idx = -(luke_pg__worker_idx + 1);
  return luke_pg__worker_idx % nshards;
}

static inline void luke_pg__waiter_free(LukePgWaiter *w) {
  if (!w) return;
  free(w->sql);
  luke_pg__free_params(w->params, w->param_lens, w->nparams);
  free(w->text);
  pthread_mutex_destroy(&w->mu);
  pthread_cond_destroy(&w->cv);
  free(w);
}

static inline void luke_pg__waiter_signal(LukePgWaiter *w, int ok, char *text) {
  pthread_mutex_lock(&w->mu);
  w->ok = ok;
  w->text = text;
  w->done = 1;
  pthread_cond_signal(&w->cv);
  pthread_mutex_unlock(&w->mu);
}

static inline void luke_pg__slot_fail_all(LukePgSlot *slot, const char *why) {
  (void)why;
  LukePgWaiter *w = slot->fifo_head;
  slot->fifo_head = slot->fifo_tail = NULL;
  slot->in_flight = 0;
  slot->in_pipeline = 0;
  slot->expecting_sync = 0;
  while (w) {
    LukePgWaiter *n = w->fifo_next;
    w->fifo_next = NULL;
    luke_pg__waiter_signal(w, 0, NULL);
    w = n;
  }
}

static inline void luke_pg__slot_stmt_clear(LukePgSlot *slot) {
  LukePgSlotStmt *s = slot->stmts;
  while (s) {
    LukePgSlotStmt *n = s->next;
    free(s->sql);
    free(s);
    s = n;
  }
  slot->stmts = NULL;
  slot->stmt_n = 0;
}

static inline const char *luke_pg__slot_ensure_prep(LukePgSlot *slot, const char *sql,
                                                     size_t sql_len) {
  for (LukePgSlotStmt *s = slot->stmts; s; s = s->next) {
    if (s->sql_len == sql_len && memcmp(s->sql, sql, sql_len) == 0) return s->name;
  }
  if (slot->stmt_n >= LUKE_PG_STMT_CACHE) return NULL;
  if (slot->in_pipeline) {
    /* Prepare must happen outside pipeline mode. */
    return NULL;
  }
  unsigned h = luke_pg__hash(sql, sql_len);
  char name[32];
  snprintf(name, sizeof(name), "luke_a_%08x", h ^ (unsigned)slot->stmt_n);
  PGresult *res = PQprepare(slot->conn, name, sql, 0, NULL);
  if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
    if (res) PQclear(res);
    return NULL;
  }
  PQclear(res);
  LukePgSlotStmt *node = (LukePgSlotStmt *)calloc(1, sizeof(LukePgSlotStmt));
  if (!node) return NULL;
  node->sql = (char *)malloc(sql_len + 1);
  if (!node->sql) {
    free(node);
    return NULL;
  }
  memcpy(node->sql, sql, sql_len);
  node->sql[sql_len] = '\0';
  node->sql_len = sql_len;
  memcpy(node->name, name, sizeof(name));
  node->next = slot->stmts;
  slot->stmts = node;
  slot->stmt_n++;
  return node->name;
}

static inline void luke_pg__fifo_push(LukePgSlot *slot, LukePgWaiter *w) {
  w->fifo_next = NULL;
  if (slot->fifo_tail) slot->fifo_tail->fifo_next = w;
  else slot->fifo_head = w;
  slot->fifo_tail = w;
  slot->in_flight++;
}

static inline LukePgWaiter *luke_pg__fifo_pop(LukePgSlot *slot) {
  LukePgWaiter *w = slot->fifo_head;
  if (!w) return NULL;
  slot->fifo_head = w->fifo_next;
  if (!slot->fifo_head) slot->fifo_tail = NULL;
  w->fifo_next = NULL;
  if (slot->in_flight > 0) slot->in_flight--;
  return w;
}

static inline char *luke_pg__cell0(PGresult *res, int row) {
  if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) return NULL;
  if (row >= PQntuples(res) || PQnfields(res) < 1) {
    char *z = (char *)malloc(1);
    if (z) z[0] = '\0';
    return z;
  }
  if (PQgetisnull(res, row, 0)) {
    char *z = (char *)malloc(1);
    if (z) z[0] = '\0';
    return z;
  }
  const char *v = PQgetvalue(res, row, 0);
  int len = PQgetlength(res, row, 0);
  char *out = (char *)malloc((size_t)len + 1);
  if (!out) return NULL;
  if (len) memcpy(out, v, (size_t)len);
  out[len] = '\0';
  return out;
}

static inline char *luke_pg__all_col0(PGresult *res) {
  if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) return NULL;
  int rows = PQntuples(res);
  size_t cap = 256, len = 0;
  char *buf = (char *)malloc(cap);
  if (!buf) return NULL;
  buf[0] = '\0';
  for (int r = 0; r < rows; ++r) {
    const char *v = PQgetisnull(res, r, 0) ? "" : PQgetvalue(res, r, 0);
    int vl = PQgetisnull(res, r, 0) ? 0 : PQgetlength(res, r, 0);
    size_t need = len + (r ? 1 : 0) + (size_t)vl + 1;
    if (need > cap) {
      while (cap < need) cap *= 2;
      char *nb = (char *)realloc(buf, cap);
      if (!nb) {
        free(buf);
        return NULL;
      }
      buf = nb;
    }
    if (r) buf[len++] = '\n';
    if (vl) memcpy(buf + len, v, (size_t)vl);
    len += (size_t)vl;
    buf[len] = '\0';
  }
  return buf;
}

static inline int luke_pg__flush_conn(LukePgSlot *slot) {
  for (;;) {
    int r = PQflush(slot->conn);
    if (r == 0) return 1;
    if (r < 0) return 0;
    struct pollfd pfd;
    pfd.fd = slot->fd;
    pfd.events = POLLOUT;
    if (poll(&pfd, 1, 1000) < 0 && errno != EINTR) return 0;
  }
}

static inline int luke_pg__send_one(LukePgSlot *slot, LukePgWaiter *w) {
  const char *name = luke_pg__slot_ensure_prep(slot, w->sql, w->sql_len);
  int ok;
  if (name) {
    ok = PQsendQueryPrepared(slot->conn, name, w->nparams, (const char *const *)w->params,
                             w->param_lens, NULL, 0);
  } else {
    ok = PQsendQueryParams(slot->conn, w->sql, w->nparams, NULL, (const char *const *)w->params,
                           w->param_lens, NULL, 0);
  }
  if (!ok) return 0;
  luke_pg__fifo_push(slot, w);
  return 1;
}

static inline void luke_pg__shard_enqueue_front(LukePgShard *sh, LukePgWaiter *w) {
  pthread_mutex_lock(&sh->qmu);
  int was_empty = (sh->q_len == 0);
  w->q_next = sh->q_head;
  sh->q_head = w;
  if (!sh->q_tail) sh->q_tail = w;
  sh->q_len++;
  pthread_mutex_unlock(&sh->qmu);
  if (was_empty) {
#if defined(__linux__)
    uint64_t one = 1;
    (void)write(sh->wake_wr, &one, sizeof(one));
#else
    char x = 1;
    (void)write(sh->wake_wr, &x, 1);
#endif
  }
}

static inline void luke_pg__dispatch(LukePgShard *sh) {
  LukePgWaiter *batch[256];
  int bn = 0;
  int depth = luke_pg__pipeline_depth();
  int draft_min = luke_pg__draft_min();
  if (depth > 256) depth = 256;

  pthread_mutex_lock(&sh->qmu);
  while (sh->q_head && bn < 256) {
    LukePgWaiter *w = sh->q_head;
    sh->q_head = w->q_next;
    if (!sh->q_head) sh->q_tail = NULL;
    sh->q_len--;
    w->q_next = NULL;
    batch[bn++] = w;
  }
  pthread_mutex_unlock(&sh->qmu);

  int bi = 0;
  while (bi < bn) {
    int best = -1;
    for (int i = 0; i < sh->nslots; ++i) {
      LukePgSlot *s = &sh->slots[i];
      if (!s->conn || PQstatus(s->conn) != CONNECTION_OK) continue;
      if (s->in_flight != 0) continue; /* only fill idle slots */
      best = i;
      break;
    }
    if (best < 0) {
      /* All busy — put remainder back (preserve order). */
      for (int j = bn - 1; j >= bi; --j) luke_pg__shard_enqueue_front(sh, batch[j]);
      break;
    }

    LukePgSlot *slot = &sh->slots[best];
    int take = bn - bi;
    if (take > depth) take = depth;

    /* Ensure prepares outside pipeline (sync, once per SQL). */
    for (int k = 0; k < take; ++k) {
      (void)luke_pg__slot_ensure_prep(slot, batch[bi + k]->sql, batch[bi + k]->sql_len);
    }

    if (take < draft_min) {
      /* Fast path: nothing drafting behind — no pipeline coordination. */
      LukePgWaiter *w = batch[bi++];
      if (!luke_pg__send_one(slot, w)) {
        luke_pg__waiter_signal(w, 0, NULL);
        continue;
      }
      slot->in_pipeline = 0;
      slot->expecting_sync = 0;
      if (!luke_pg__flush_conn(slot)) {
        luke_pg__slot_fail_all(slot, "flush");
        PQfinish(slot->conn);
        slot->conn = NULL;
      }
      continue;
    }

    /* Batch path: draft k queries through one RTT. */
    if (!PQenterPipelineMode(slot->conn)) {
      for (int k = 0; k < take; ++k) luke_pg__waiter_signal(batch[bi + k], 0, NULL);
      bi += take;
      continue;
    }
    slot->in_pipeline = 1;
    int sent = 0;
    for (int k = 0; k < take; ++k) {
      LukePgWaiter *w = batch[bi + k];
      if (!luke_pg__send_one(slot, w)) {
        luke_pg__waiter_signal(w, 0, NULL);
        continue;
      }
      sent++;
    }
    bi += take;
    if (sent == 0) {
      (void)PQexitPipelineMode(slot->conn);
      slot->in_pipeline = 0;
      continue;
    }
    if (!PQpipelineSync(slot->conn)) {
      luke_pg__slot_fail_all(slot, "sync");
      (void)PQexitPipelineMode(slot->conn);
      slot->in_pipeline = 0;
      continue;
    }
    slot->expecting_sync = 1;
    if (!luke_pg__flush_conn(slot)) {
      luke_pg__slot_fail_all(slot, "flush");
      PQfinish(slot->conn);
      slot->conn = NULL;
    }
  }
}

static inline void luke_pg__on_readable(LukePgShard *sh, LukePgSlot *slot) {
  if (!slot->conn) return;
  if (!PQconsumeInput(slot->conn)) {
    luke_pg__slot_fail_all(slot, "consume");
    PQfinish(slot->conn);
    slot->conn = NULL;
    return;
  }
  for (;;) {
    if (PQisBusy(slot->conn)) break;
    PGresult *res = PQgetResult(slot->conn);
    if (!res) {
      /* End of one statement. Keep draining if more in-flight or sync pending. */
      if (slot->in_flight > 0 || slot->expecting_sync) {
        if (PQisBusy(slot->conn)) break;
        continue;
      }
      break;
    }
    ExecStatusType st = PQresultStatus(res);
    if (st == PGRES_PIPELINE_SYNC) {
      PQclear(res);
      (void)PQexitPipelineMode(slot->conn);
      slot->in_pipeline = 0;
      slot->expecting_sync = 0;
      continue;
    }
    if (st == PGRES_PIPELINE_ABORTED) {
      /* Neighbor aborted after a prior error — requeue so it stays alive. */
      LukePgWaiter *w = luke_pg__fifo_pop(slot);
      PQclear(res);
      if (w) luke_pg__shard_enqueue_front(sh, w);
      continue;
    }
    LukePgWaiter *w = luke_pg__fifo_pop(slot);
    if (!w) {
      PQclear(res);
      continue;
    }
    if (st == PGRES_TUPLES_OK) {
      char *text = w->want_rows ? luke_pg__all_col0(res) : luke_pg__cell0(res, 0);
      PQclear(res);
      luke_pg__waiter_signal(w, text != NULL, text);
    } else if (st == PGRES_COMMAND_OK) {
      PQclear(res);
      char *z = (char *)malloc(1);
      if (z) z[0] = '\0';
      luke_pg__waiter_signal(w, 1, z);
    } else {
      /* Fail only this waiter; pipeline stays aborted until sync. */
      PQclear(res);
      luke_pg__waiter_signal(w, 0, NULL);
    }
  }
  if (PQstatus(slot->conn) == CONNECTION_BAD) {
    luke_pg__slot_fail_all(slot, "bad");
    PQfinish(slot->conn);
    slot->conn = NULL;
  }
}

static inline int luke_pg__reconnect_slot(LukePgShard *sh, LukePgSlot *slot) {
  if (slot->conn) {
    luke_pg__slot_fail_all(slot, "reconnect");
    PQfinish(slot->conn);
    slot->conn = NULL;
  }
  luke_pg__slot_stmt_clear(slot);
  slot->conn = PQconnectdb(sh->conninfo);
  if (!slot->conn || PQstatus(slot->conn) != CONNECTION_OK) {
    if (slot->conn) {
      PQfinish(slot->conn);
      slot->conn = NULL;
    }
    return 0;
  }
  PQsetnonblocking(slot->conn, 1);
  slot->fd = PQsocket(slot->conn);
  slot->in_pipeline = 0;
  slot->expecting_sync = 0;
  return slot->fd >= 0;
}

static inline void luke_pg__wake(LukePgShard *sh) {
#if defined(__linux__)
  uint64_t one = 1;
  (void)write(sh->wake_wr, &one, sizeof(one));
#else
  char x = 1;
  (void)write(sh->wake_wr, &x, 1);
#endif
}

static inline void *luke_pg__io_thread(void *arg) {
  LukePgShard *sh = (LukePgShard *)arg;
#if defined(__linux__)
  struct epoll_event evs[64];
  for (int i = 0; i < sh->nslots; ++i) {
    if (sh->slots[i].fd < 0) continue;
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.u32 = (uint32_t)i;
    epoll_ctl(sh->epfd, EPOLL_CTL_ADD, sh->slots[i].fd, &ev);
  }
  {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.u32 = 0xffffffffu;
    epoll_ctl(sh->epfd, EPOLL_CTL_ADD, sh->wake_fd, &ev);
  }
  while (!sh->stop) {
    int n = epoll_wait(sh->epfd, evs, 64, 200);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    int woke = 0;
    for (int i = 0; i < n; ++i) {
      if (evs[i].data.u32 == 0xffffffffu) {
        uint64_t x;
        (void)read(sh->wake_fd, &x, sizeof(x));
        woke = 1;
      } else {
        unsigned idx = evs[i].data.u32;
        if (idx < (unsigned)sh->nslots) luke_pg__on_readable(sh, &sh->slots[idx]);
      }
    }
    /* Always dispatch: completions free slots; aborted waiters may be requeued. */
    (void)woke;
    luke_pg__dispatch(sh);
    for (int i = 0; i < sh->nslots; ++i) {
      if (!sh->slots[i].conn && !sh->stop) {
        if (luke_pg__reconnect_slot(sh, &sh->slots[i])) {
          struct epoll_event ev;
          memset(&ev, 0, sizeof(ev));
          ev.events = EPOLLIN;
          ev.data.u32 = (uint32_t)i;
          epoll_ctl(sh->epfd, EPOLL_CTL_ADD, sh->slots[i].fd, &ev);
        }
      }
    }
  }
#else
  while (!sh->stop) {
    luke_pg__dispatch(sh);
    struct pollfd pfds[66];
    int np = 0;
    pfds[np].fd = sh->wake_fd;
    pfds[np].events = POLLIN;
    pfds[np].revents = 0;
    int wake_i = np++;
    int map[64];
    for (int i = 0; i < sh->nslots; ++i) {
      if (!sh->slots[i].conn || sh->slots[i].fd < 0) continue;
      map[np] = i;
      pfds[np].fd = sh->slots[i].fd;
      pfds[np].events = POLLIN;
      pfds[np].revents = 0;
      np++;
    }
    int pr = poll(pfds, (nfds_t)np, 200);
    if (pr < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (pfds[wake_i].revents & POLLIN) {
      char buf[64];
      (void)read(sh->wake_fd, buf, sizeof(buf));
    }
    for (int i = 1; i < np; ++i) {
      if (pfds[i].revents & (POLLIN | POLLERR | POLLHUP))
        luke_pg__on_readable(sh, &sh->slots[map[i]]);
    }
  }
#endif
  return NULL;
}

static inline int luke_pg__shard_start(LukePgShard *sh, const char *conninfo, int nslots) {
  memset(sh, 0, sizeof(*sh));
  sh->conninfo = (char *)conninfo;
  sh->nslots = nslots;
  sh->slots = (LukePgSlot *)calloc((size_t)nslots, sizeof(LukePgSlot));
  if (!sh->slots) return 0;
  for (int i = 0; i < nslots; ++i) sh->slots[i].fd = -1;
  pthread_mutex_init(&sh->qmu, NULL);
#if defined(__linux__)
  sh->epfd = epoll_create1(EPOLL_CLOEXEC);
  sh->wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  sh->wake_wr = sh->wake_fd;
  if (sh->epfd < 0 || sh->wake_fd < 0) return 0;
#else
  sh->epfd = -1;
  int pfd[2];
  if (pipe(pfd) != 0) return 0;
  sh->wake_fd = pfd[0];
  sh->wake_wr = pfd[1];
  fcntl(sh->wake_fd, F_SETFL, O_NONBLOCK);
#endif
  int ok_any = 0;
  for (int i = 0; i < nslots; ++i) {
    if (luke_pg__reconnect_slot(sh, &sh->slots[i])) ok_any = 1;
  }
  if (!ok_any) return 0;
  if (pthread_create(&sh->thr, NULL, luke_pg__io_thread, sh) != 0) return 0;
  sh->started = 1;
  return 1;
}

static inline void luke_pg__shard_fail_cleanup(LukePgShard *sh) {
  if (!sh->slots) return;
  for (int i = 0; i < sh->nslots; ++i) {
    luke_pg__slot_stmt_clear(&sh->slots[i]);
    if (sh->slots[i].conn) PQfinish(sh->slots[i].conn);
  }
  free(sh->slots);
  sh->slots = NULL;
  if (sh->wake_fd >= 0) close(sh->wake_fd);
#if !defined(__linux__)
  if (sh->wake_wr >= 0 && sh->wake_wr != sh->wake_fd) close(sh->wake_wr);
#endif
  if (sh->epfd >= 0) close(sh->epfd);
  pthread_mutex_destroy(&sh->qmu);
}

static inline LukePgPool *luke_pg__pool_get(const char *conninfo) {
  pthread_mutex_lock(&luke_pg__pool_mu);
  for (LukePgPool *p = luke_pg__pools; p; p = p->next) {
    if (p->conninfo && strcmp(p->conninfo, conninfo) == 0) {
      pthread_mutex_unlock(&luke_pg__pool_mu);
      return p;
    }
  }
  int nshards = luke_pg__nshards();
  int total = luke_pg__nconns();
  if (nshards > total) nshards = total;
  LukePgPool *pool = (LukePgPool *)calloc(1, sizeof(LukePgPool));
  if (!pool) {
    pthread_mutex_unlock(&luke_pg__pool_mu);
    return NULL;
  }
  pool->conninfo = strdup(conninfo);
  pool->nshards = nshards;
  pool->shards = (LukePgShard *)calloc((size_t)nshards, sizeof(LukePgShard));
  if (!pool->conninfo || !pool->shards) {
    free(pool->conninfo);
    free(pool->shards);
    free(pool);
    pthread_mutex_unlock(&luke_pg__pool_mu);
    return NULL;
  }
  int base = total / nshards;
  int rem = total % nshards;
  for (int i = 0; i < nshards; ++i) {
    int n = base + (i < rem ? 1 : 0);
    if (n < 1) n = 1;
    if (!luke_pg__shard_start(&pool->shards[i], pool->conninfo, n)) {
      for (int j = 0; j < i; ++j) {
        pool->shards[j].stop = 1;
        luke_pg__wake(&pool->shards[j]);
        if (pool->shards[j].started) pthread_join(pool->shards[j].thr, NULL);
        luke_pg__shard_fail_cleanup(&pool->shards[j]);
      }
      luke_pg__shard_fail_cleanup(&pool->shards[i]);
      free(pool->shards);
      free(pool->conninfo);
      free(pool);
      pthread_mutex_unlock(&luke_pg__pool_mu);
      return NULL;
    }
  }
  pool->next = luke_pg__pools;
  luke_pg__pools = pool;
  pthread_mutex_unlock(&luke_pg__pool_mu);
  return pool;
}

static inline int luke_pg__submit(LukePgShard *sh, LukePgWaiter *w) {
  pthread_mutex_lock(&sh->qmu);
  if (sh->q_len >= LUKE_PG_QUEUE) {
    pthread_mutex_unlock(&sh->qmu);
    return 0;
  }
  int was_empty = (sh->q_len == 0);
  w->q_next = NULL;
  if (sh->q_tail) sh->q_tail->q_next = w;
  else sh->q_head = w;
  sh->q_tail = w;
  sh->q_len++;
  pthread_mutex_unlock(&sh->qmu);
  /* Wake only on 0→non-empty edge — burst costs one wakeup. */
  if (was_empty) luke_pg__wake(sh);
  pthread_mutex_lock(&w->mu);
  while (!w->done) pthread_cond_wait(&w->cv, &w->mu);
  int ok = w->ok;
  pthread_mutex_unlock(&w->mu);
  return ok;
}

static inline LukePgShard *luke_pg__pick_shard(LukePg *pg) {
  LukePgPool *pool = luke_pg__pool_get(pg->conninfo);
  if (!pool || pool->nshards < 1) return NULL;
  return &pool->shards[luke_pg__worker_shard(pool->nshards)];
}

static inline LukeText luke_pg__async_query(LukeArena *a, LukePg *pg, LukeText sql, LukeList *params,
                                            int want_rows) {
  LukePgShard *sh = luke_pg__pick_shard(pg);
  if (!sh) return luke_text("");
  LukePgWaiter *w = (LukePgWaiter *)calloc(1, sizeof(LukePgWaiter));
  if (!w) return luke_text("");
  pthread_mutex_init(&w->mu, NULL);
  pthread_cond_init(&w->cv, NULL);
  w->sql = (char *)malloc(sql.len + 1);
  if (!w->sql) {
    luke_pg__waiter_free(w);
    return luke_text("");
  }
  if (sql.len) memcpy(w->sql, sql.ptr, sql.len);
  w->sql[sql.len] = '\0';
  w->sql_len = sql.len;
  w->params = luke_pg__dup_params(params, &w->nparams, &w->param_lens);
  w->want_rows = want_rows;
  if (!luke_pg__submit(sh, w)) {
    luke_pg__waiter_free(w);
    return luke_text("");
  }
  LukeText out = luke_text("");
  if (w->ok && w->text) {
    size_t n = strlen(w->text);
    char *p = (char *)luke_arena_alloc(a, n + 1, 1);
    memcpy(p, w->text, n + 1);
    out = luke_text_n(p, n);
  }
  luke_pg__waiter_free(w);
  return out;
}

static inline int luke_pg__async_exec(LukePg *pg, LukeText sql, LukeList *params) {
  LukePgShard *sh = luke_pg__pick_shard(pg);
  if (!sh) return 0;
  LukePgWaiter *w = (LukePgWaiter *)calloc(1, sizeof(LukePgWaiter));
  if (!w) return 0;
  pthread_mutex_init(&w->mu, NULL);
  pthread_cond_init(&w->cv, NULL);
  w->sql = (char *)malloc(sql.len + 1);
  if (!w->sql) {
    luke_pg__waiter_free(w);
    return 0;
  }
  if (sql.len) memcpy(w->sql, sql.ptr, sql.len);
  w->sql[sql.len] = '\0';
  w->sql_len = sql.len;
  w->params = luke_pg__dup_params(params, &w->nparams, &w->param_lens);
  w->want_rows = 0;
  int ok = luke_pg__submit(sh, w);
  int rc = ok && w->ok;
  luke_pg__waiter_free(w);
  return rc;
}

/* ---------- public API ---------- */

static inline LukePg *luke_pg_open(LukeArena *a, LukeText conninfo) {
  (void)a;
  char ci[2048];
  luke_pg__cstr(conninfo, ci, sizeof(ci));
  size_t cilen = strlen(ci);
  if (!cilen) return NULL;

  if (luke_pg__pool_enabled()) {
    LukePg *hit = luke_pg__tls_find(ci, cilen);
    if (hit) {
      hit->refs++;
      return hit;
    }
  }

  LukePg *pg = (LukePg *)calloc(1, sizeof(LukePg));
  if (!pg) return NULL;
  pg->conninfo = (char *)malloc(cilen + 1);
  if (!pg->conninfo) {
    free(pg);
    return NULL;
  }
  memcpy(pg->conninfo, ci, cilen + 1);
  pg->conninfo_len = cilen;
  pg->refs = 1;
  pg->pooled = luke_pg__pool_enabled();

  if (!luke_pg__async_enabled()) {
    pg->blocking = PQconnectdb(ci);
    if (!pg->blocking || PQstatus(pg->blocking) != CONNECTION_OK) {
      luke_pg__destroy(pg);
      return NULL;
    }
  } else {
    if (!luke_pg__pool_get(ci)) {
      luke_pg__destroy(pg);
      return NULL;
    }
  }

  if (pg->pooled) {
    pg->next = luke_pg__tls;
    luke_pg__tls = pg;
  }
  return pg;
}

static inline int luke_pg_close(LukePg *pg) {
  if (!pg) return 0;
  if (pg->pooled) {
    if (pg->refs > 0) pg->refs--;
    return 1;
  }
  if (luke_pg__tls == pg)
    luke_pg__tls = pg->next;
  else {
    for (LukePg *p = luke_pg__tls; p; p = p->next) {
      if (p->next == pg) {
        p->next = pg->next;
        break;
      }
    }
  }
  luke_pg__destroy(pg);
  return 1;
}

/* Pin a dedicated connection for BEGIN…COMMIT — never drafted into a shared pipeline. */
static inline int luke_pg_checkout(LukePg *pg) {
  if (!pg) return 0;
  if (pg->pinned) return 1;
  pg->pinned = PQconnectdb(pg->conninfo);
  if (!pg->pinned || PQstatus(pg->pinned) != CONNECTION_OK) {
    if (pg->pinned) {
      PQfinish(pg->pinned);
      pg->pinned = NULL;
    }
    return 0;
  }
  luke_pg__stmt_clear(pg);
  return 1;
}

static inline int luke_pg_checkin(LukePg *pg) {
  if (!pg) return 0;
  if (!pg->pinned) return 1;
  luke_pg__stmt_clear(pg);
  PQfinish(pg->pinned);
  pg->pinned = NULL;
  return 1;
}

static inline int luke_pg_exec_bind(LukePg *pg, LukeText sql, LukeList *params) {
  if (!pg) return 0;
  if (pg->pinned) {
    PGresult *res = luke_pg__exec_params(pg, sql.ptr ? sql.ptr : "", sql.len, params);
    int ok = luke_pg__result_ok(res);
    if (res) PQclear(res);
    return ok;
  }
  if (luke_pg__async_enabled() || !pg->blocking) return luke_pg__async_exec(pg, sql, params);
  PGresult *res = luke_pg__exec_params(pg, sql.ptr ? sql.ptr : "", sql.len, params);
  int ok = luke_pg__result_ok(res);
  if (res) PQclear(res);
  return ok;
}

static inline LukeText luke_pg_query_bind(LukeArena *a, LukePg *pg, LukeText sql, LukeList *params) {
  if (!pg) return luke_text("");
  if (pg->pinned || (!luke_pg__async_enabled() && pg->blocking)) {
    PGresult *res = luke_pg__exec_params(pg, sql.ptr ? sql.ptr : "", sql.len, params);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
      if (res) PQclear(res);
      return luke_text("");
    }
    char *cell = luke_pg__cell0(res, 0);
    PQclear(res);
    if (!cell) return luke_text("");
    size_t n = strlen(cell);
    char *p = (char *)luke_arena_alloc(a, n + 1, 1);
    memcpy(p, cell, n + 1);
    free(cell);
    return luke_text_n(p, n);
  }
  return luke_pg__async_query(a, pg, sql, params, 0);
}

static inline LukeText luke_pg_rows_bind(LukeArena *a, LukePg *pg, LukeText sql, LukeList *params) {
  if (!pg) return luke_text("");
  if (pg->pinned || (!luke_pg__async_enabled() && pg->blocking)) {
    PGresult *res = luke_pg__exec_params(pg, sql.ptr ? sql.ptr : "", sql.len, params);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
      if (res) PQclear(res);
      return luke_text("");
    }
    char *rows = luke_pg__all_col0(res);
    PQclear(res);
    if (!rows) return luke_text("");
    size_t n = strlen(rows);
    char *p = (char *)luke_arena_alloc(a, n + 1, 1);
    memcpy(p, rows, n + 1);
    free(rows);
    return luke_text_n(p, n);
  }
  return luke_pg__async_query(a, pg, sql, params, 1);
}

#endif /* !__wasi__ */

#ifdef __cplusplus
}
#endif

#endif /* LUKE_PG_H */
