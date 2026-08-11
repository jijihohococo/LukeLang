#ifndef LUKE_AUTH_H
#define LUKE_AUTH_H

/* LukeLang auth beachhead — libsodium only (Argon2id via crypto_pwhash, CSPRNG).
 * No homegrown crypto. Password = hash (never encrypt). Sessions + CSRF are first-class. */

#include "luke_rt.h"
#include "luke_db.h"
#include "luke_net.h"

#if !defined(__wasi__)
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Cookie attribute flags for luke_http_set_cookie_ex */
#ifndef LUKE_COOKIE_HTTPONLY
#define LUKE_COOKIE_HTTPONLY 1
#define LUKE_COOKIE_SECURE 2
#define LUKE_COOKIE_SAMESITE_LAX 4
#define LUKE_COOKIE_SAMESITE_STRICT 8
#define LUKE_COOKIE_CLEAR 16
#endif

#if defined(__wasi__)

static inline int luke_auth_init(LukeDb *db) {
  (void)db;
  return 0;
}
static inline LukeText luke_auth_create_account(LukeArena *a, LukeDb *db, LukeText email,
                                                LukeText password) {
  (void)a;
  (void)db;
  (void)email;
  (void)password;
  return luke_text("");
}
static inline LukeText luke_auth_login(LukeArena *a, LukeDb *db, LukeHttpRequest *req,
                                       LukeText email, LukeText password) {
  (void)a;
  (void)db;
  (void)req;
  (void)email;
  (void)password;
  return luke_text("");
}
static inline int luke_auth_logout(LukeArena *a, LukeDb *db, LukeHttpRequest *req) {
  (void)a;
  (void)db;
  (void)req;
  return 0;
}
static inline int luke_auth_require(LukeArena *a, LukeDb *db, LukeHttpRequest *req) {
  (void)a;
  (void)db;
  (void)req;
  return 0;
}
static inline LukeText luke_auth_current_user(void) { return luke_text(""); }
static inline LukeText luke_auth_csrf(LukeArena *a, LukeHttpRequest *req) {
  (void)a;
  (void)req;
  return luke_text("");
}
static inline int luke_auth_check_csrf(LukeArena *a, LukeDb *db, LukeHttpRequest *req) {
  (void)a;
  (void)db;
  (void)req;
  return 0;
}
static inline LukeText luke_auth_user_sql(LukeArena *a, LukeText sql) {
  (void)a;
  (void)sql;
  return luke_text("");
}

#else /* !__wasi__ */

#ifndef LUKE_AUTH_SESSION_DAYS
#define LUKE_AUTH_SESSION_DAYS 7
#endif
#ifndef LUKE_AUTH_MAX_FAILS
#define LUKE_AUTH_MAX_FAILS 5
#endif
#ifndef LUKE_AUTH_LOCK_SECS
#define LUKE_AUTH_LOCK_SECS (15 * 60)
#endif

static __thread char luke_auth_tls_uid[65];
static __thread char luke_auth_tls_csrf[65];
static __thread int luke_auth_tls_ok;
static int luke_auth_sodium_ready;

static inline int luke_auth__sodium(void) {
  if (luke_auth_sodium_ready) return 1;
  if (sodium_init() < 0) return 0;
  luke_auth_sodium_ready = 1;
  return 1;
}

static inline int luke_auth__secure_cookies(void) {
  const char *e = getenv("LUKE_AUTH_SECURE");
  return e && e[0] && e[0] != '0';
}

static inline LukeText luke_auth__hex(LukeArena *a, size_t nbytes) {
  unsigned char raw[64];
  if (nbytes > sizeof(raw)) nbytes = sizeof(raw);
  randombytes_buf(raw, nbytes);
  char *p = (char *)luke_arena_alloc(a, nbytes * 2 + 1, 1);
  static const char *hex = "0123456789abcdef";
  for (size_t i = 0; i < nbytes; ++i) {
    p[i * 2] = hex[(raw[i] >> 4) & 0xf];
    p[i * 2 + 1] = hex[raw[i] & 0xf];
  }
  p[nbytes * 2] = '\0';
  return luke_text_n(p, nbytes * 2);
}

static inline int luke_auth__is_hex(LukeText t) {
  if (!t.len || t.len > 64) return 0;
  for (size_t i = 0; i < t.len; ++i) {
    char c = t.ptr[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
  }
  return 1;
}

static inline LukeText luke_auth__norm_email(LukeArena *a, LukeText email) {
  size_t n = email.len;
  while (n && (email.ptr[0] == ' ' || email.ptr[0] == '\t')) {
    email.ptr++;
    email.len--;
    n = email.len;
  }
  while (n && (email.ptr[n - 1] == ' ' || email.ptr[n - 1] == '\t')) n--;
  char *p = (char *)luke_arena_alloc(a, n + 1, 1);
  for (size_t i = 0; i < n; ++i) {
    char c = email.ptr[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    p[i] = c;
  }
  p[n] = '\0';
  return luke_text_n(p, n);
}

static inline int luke_auth__email_ok(LukeText e) {
  if (e.len < 3 || e.len > 254) return 0;
  int at = 0;
  for (size_t i = 0; i < e.len; ++i) {
    char c = e.ptr[i];
    if (c == '@') at++;
    if (c < 33 || c > 126) return 0;
  }
  return at == 1;
}

static inline const char *luke_auth__dummy_hash(void) {
  static char dummy[crypto_pwhash_STRBYTES];
  static int ready = 0;
  if (!ready) {
    /* Timing cover for unknown emails — never used as a real password store. */
    (void)crypto_pwhash_str(dummy, "!", 1, crypto_pwhash_OPSLIMIT_INTERACTIVE,
                            crypto_pwhash_MEMLIMIT_INTERACTIVE);
    ready = 1;
  }
  return dummy;
}

static inline int luke_auth_init(LukeDb *db) {
  if (!luke_auth__sodium()) return 0;
  if (!db) return 0;
  if (!luke_db_exec(db, luke_text("CREATE TABLE IF NOT EXISTS luke_users("
                                  "id TEXT PRIMARY KEY, email TEXT UNIQUE NOT NULL, "
                                  "phash TEXT NOT NULL, created_at INTEGER NOT NULL)")))
    return 0;
  if (!luke_db_exec(db, luke_text("CREATE TABLE IF NOT EXISTS luke_sessions("
                                  "sid TEXT PRIMARY KEY, user_id TEXT NOT NULL, "
                                  "csrf TEXT NOT NULL, expires_at INTEGER NOT NULL)")))
    return 0;
  if (!luke_db_exec(db, luke_text("CREATE TABLE IF NOT EXISTS luke_auth_attempts("
                                  "email TEXT PRIMARY KEY, fails INTEGER NOT NULL, "
                                  "window_start INTEGER NOT NULL)")))
    return 0;
  return 1;
}

static inline int64_t luke_auth__now(void) { return (int64_t)time(NULL); }

static inline int luke_auth__locked(LukeArena *a, LukeDb *db, LukeText email) {
  LukeList *p = luke_list_new(a);
  luke_list_add(a, p, email);
  LukeText row = luke_db_query_bind_text(
      a, db, luke_text("SELECT fails || ' ' || window_start FROM luke_auth_attempts WHERE email = ?"),
      p);
  if (!row.len) return 0;
  long fails = 0, win = 0;
  sscanf(row.ptr, "%ld %ld", &fails, &win);
  int64_t now = luke_auth__now();
  if (fails >= LUKE_AUTH_MAX_FAILS && (now - win) < LUKE_AUTH_LOCK_SECS) return 1;
  if ((now - win) >= LUKE_AUTH_LOCK_SECS) {
    LukeList *c = luke_list_new(a);
    luke_list_add(a, c, email);
    luke_db_exec_bind(db, luke_text("DELETE FROM luke_auth_attempts WHERE email = ?"), c);
  }
  return 0;
}

static inline void luke_auth__fail(LukeArena *a, LukeDb *db, LukeText email) {
  LukeList *p = luke_list_new(a);
  luke_list_add(a, p, email);
  LukeText row = luke_db_query_bind_text(
      a, db, luke_text("SELECT fails || ' ' || window_start FROM luke_auth_attempts WHERE email = ?"),
      p);
  int64_t now = luke_auth__now();
  long fails = 0, win = now;
  if (row.len) sscanf(row.ptr, "%ld %ld", &fails, &win);
  if ((now - win) >= LUKE_AUTH_LOCK_SECS) {
    fails = 0;
    win = now;
  }
  fails++;
  char fb[32], wb[32];
  snprintf(fb, sizeof(fb), "%ld", fails);
  snprintf(wb, sizeof(wb), "%lld", (long long)win);
  LukeList *u = luke_list_new(a);
  luke_list_add(a, u, email);
  luke_list_add(a, u, luke_text(fb));
  luke_list_add(a, u, luke_text(wb));
  luke_db_exec_bind(db,
                    luke_text("INSERT INTO luke_auth_attempts(email, fails, window_start) VALUES(?,?,?) "
                              "ON CONFLICT(email) DO UPDATE SET fails=excluded.fails, "
                              "window_start=excluded.window_start"),
                    u);
}

static inline void luke_auth__clear_fails(LukeArena *a, LukeDb *db, LukeText email) {
  LukeList *p = luke_list_new(a);
  luke_list_add(a, p, email);
  luke_db_exec_bind(db, luke_text("DELETE FROM luke_auth_attempts WHERE email = ?"), p);
}

static inline LukeText luke_auth_create_account(LukeArena *a, LukeDb *db, LukeText email,
                                                LukeText password) {
  if (!luke_auth__sodium() || !a || !db) return luke_text("");
  LukeText em = luke_auth__norm_email(a, email);
  if (!luke_auth__email_ok(em) || password.len < 8 || password.len > 1024) return luke_text("");
  char phash[crypto_pwhash_STRBYTES];
  if (crypto_pwhash_str(phash, password.ptr ? password.ptr : "", password.len,
                        crypto_pwhash_OPSLIMIT_INTERACTIVE,
                        crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
    return luke_text("");
  LukeText uid = luke_auth__hex(a, 16);
  char ts[32];
  snprintf(ts, sizeof(ts), "%lld", (long long)luke_auth__now());
  LukeList *p = luke_list_new(a);
  luke_list_add(a, p, uid);
  luke_list_add(a, p, em);
  luke_list_add(a, p, luke_text(phash));
  luke_list_add(a, p, luke_text(ts));
  if (!luke_db_exec_bind(db, luke_text("INSERT INTO luke_users(id, email, phash, created_at) VALUES(?,?,?,?)"),
                         p))
    return luke_text("");
  return uid;
}

static inline void luke_auth__set_tls(LukeText uid, LukeText csrf) {
  luke_auth_tls_ok = 0;
  luke_auth_tls_uid[0] = 0;
  luke_auth_tls_csrf[0] = 0;
  if (!uid.len || uid.len >= sizeof(luke_auth_tls_uid) || !luke_auth__is_hex(uid)) return;
  memcpy(luke_auth_tls_uid, uid.ptr, uid.len);
  luke_auth_tls_uid[uid.len] = 0;
  if (csrf.len && csrf.len < sizeof(luke_auth_tls_csrf) && luke_auth__is_hex(csrf)) {
    memcpy(luke_auth_tls_csrf, csrf.ptr, csrf.len);
    luke_auth_tls_csrf[csrf.len] = 0;
  }
  luke_auth_tls_ok = 1;
}

static inline LukeText luke_auth_login(LukeArena *a, LukeDb *db, LukeHttpRequest *req, LukeText email,
                                       LukeText password) {
  if (!luke_auth__sodium() || !a || !db || !req) return luke_text("");
  LukeText em = luke_auth__norm_email(a, email);
  if (!luke_auth__email_ok(em) || password.len < 1) return luke_text("");

  /* Always burn an Argon2 verify — even when locked / unknown — to blunt timing leaks. */
  int locked = luke_auth__locked(a, db, em);
  LukeList *q = luke_list_new(a);
  luke_list_add(a, q, em);
  LukeText phash = luke_db_query_bind_text(a, db, luke_text("SELECT phash FROM luke_users WHERE email = ?"), q);
  LukeText uid = luke_db_query_bind_text(a, db, luke_text("SELECT id FROM luke_users WHERE email = ?"), q);
  const char *verify_against = phash.len ? phash.ptr : luke_auth__dummy_hash();
  int ok = crypto_pwhash_str_verify(verify_against, password.ptr ? password.ptr : "", password.len) == 0;
  if (locked || !phash.len || !ok || !uid.len) {
    if (!locked) luke_auth__fail(a, db, em);
    return luke_text("");
  }
  luke_auth__clear_fails(a, db, em);

  LukeText sid = luke_auth__hex(a, 32);
  LukeText csrf = luke_auth__hex(a, 32);
  int64_t exp = luke_auth__now() + (int64_t)LUKE_AUTH_SESSION_DAYS * 86400;
  char eb[32];
  snprintf(eb, sizeof(eb), "%lld", (long long)exp);
  LukeList *ins = luke_list_new(a);
  luke_list_add(a, ins, sid);
  luke_list_add(a, ins, uid);
  luke_list_add(a, ins, csrf);
  luke_list_add(a, ins, luke_text(eb));
  if (!luke_db_exec_bind(db, luke_text("INSERT INTO luke_sessions(sid, user_id, csrf, expires_at) VALUES(?,?,?,?)"),
                         ins))
    return luke_text("");

  int flags = LUKE_COOKIE_HTTPONLY | LUKE_COOKIE_SAMESITE_LAX;
  if (luke_auth__secure_cookies()) flags |= LUKE_COOKIE_SECURE;
  luke_http_set_cookie_ex(a, req, luke_text("luke_sid"), sid, flags);
  luke_auth__set_tls(uid, csrf);
  if (req) {
    req->user_id = uid;
    req->csrf = csrf;
  }
  return uid;
}

static inline int luke_auth_logout(LukeArena *a, LukeDb *db, LukeHttpRequest *req) {
  if (!a || !db || !req) return 0;
  LukeText sid = luke_http_cookie(a, req, luke_text("luke_sid"));
  if (sid.len) {
    LukeList *p = luke_list_new(a);
    luke_list_add(a, p, sid);
    luke_db_exec_bind(db, luke_text("DELETE FROM luke_sessions WHERE sid = ?"), p);
  }
  int flags = LUKE_COOKIE_HTTPONLY | LUKE_COOKIE_SAMESITE_LAX | LUKE_COOKIE_CLEAR;
  if (luke_auth__secure_cookies()) flags |= LUKE_COOKIE_SECURE;
  luke_http_set_cookie_ex(a, req, luke_text("luke_sid"), luke_text(""), flags);
  luke_auth_tls_ok = 0;
  luke_auth_tls_uid[0] = 0;
  luke_auth_tls_csrf[0] = 0;
  req->user_id = luke_text("");
  req->csrf = luke_text("");
  return 1;
}

static inline int luke_auth_require(LukeArena *a, LukeDb *db, LukeHttpRequest *req) {
  if (!luke_auth__sodium() || !a || !db || !req) return 0;
  LukeText sid = luke_http_cookie(a, req, luke_text("luke_sid"));
  if (!sid.len || !luke_auth__is_hex(sid)) return 0;
  LukeList *p = luke_list_new(a);
  luke_list_add(a, p, sid);
  char nowb[32];
  snprintf(nowb, sizeof(nowb), "%lld", (long long)luke_auth__now());
  luke_list_add(a, p, luke_text(nowb));
  LukeText uid = luke_db_query_bind_text(
      a, db, luke_text("SELECT user_id FROM luke_sessions WHERE sid = ? AND expires_at > ?"), p);
  LukeText csrf = luke_db_query_bind_text(
      a, db, luke_text("SELECT csrf FROM luke_sessions WHERE sid = ? AND expires_at > ?"), p);
  if (!uid.len || !luke_auth__is_hex(uid)) return 0;
  luke_auth__set_tls(uid, csrf);
  req->user_id = uid;
  req->csrf = csrf;
  return 1;
}

static inline LukeText luke_auth_current_user(void) {
  if (!luke_auth_tls_ok || !luke_auth_tls_uid[0]) return luke_text("");
  return luke_text(luke_auth_tls_uid);
}

static inline LukeText luke_auth_csrf(LukeArena *a, LukeHttpRequest *req) {
  (void)a;
  if (req && req->csrf.len) return req->csrf;
  if (luke_auth_tls_ok && luke_auth_tls_csrf[0]) return luke_text(luke_auth_tls_csrf);
  return luke_text("");
}

static inline LukeText luke_auth__header_csrf(LukeArena *a, LukeHttpRequest *req) {
  LukeText h = luke_http_header(a, req, luke_text("x-csrf-token"));
  if (h.len) return h;
  return luke_http_header(a, req, luke_text("x-luke-csrf"));
}

static inline int luke_auth_check_csrf(LukeArena *a, LukeDb *db, LukeHttpRequest *req) {
  if (!luke_auth_require(a, db, req)) return 0;
  LukeText expect = luke_auth_csrf(a, req);
  LukeText got = luke_auth__header_csrf(a, req);
  if (!expect.len || !got.len || expect.len != got.len) return 0;
  return sodium_memcmp(expect.ptr, got.ptr, expect.len) == 0;
}

/* Append AND user_id = '<hex>' for Live Graph scoped watches. uid must already be hex. */
static inline LukeText luke_auth_user_sql(LukeArena *a, LukeText sql) {
  LukeText uid = luke_auth_current_user();
  if (!uid.len || !luke_auth__is_hex(uid)) return luke_text("");
  const char *suffix = " AND user_id = '";
  size_t n = sql.len + strlen(suffix) + uid.len + 1;
  char *p = (char *)luke_arena_alloc(a, n + 1, 1);
  size_t o = 0;
  if (sql.len && sql.ptr) {
    memcpy(p, sql.ptr, sql.len);
    o = sql.len;
  }
  memcpy(p + o, suffix, strlen(suffix));
  o += strlen(suffix);
  memcpy(p + o, uid.ptr, uid.len);
  o += uid.len;
  p[o++] = '\'';
  p[o] = '\0';
  return luke_text_n(p, o);
}

#endif /* !__wasi__ */

#ifdef __cplusplus
}
#endif

#endif /* LUKE_AUTH_H */
