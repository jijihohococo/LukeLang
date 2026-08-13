#ifndef LUKE_NET_H
#define LUKE_NET_H

/* Minimal HTTP/1.1 server for Luke Build mode (native only; stubbed on WASI). */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "luke_rt.h"

#if !defined(__wasi__)
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/epoll.h>
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/event.h>
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LukeHttpServer {
  int fd;
} LukeHttpServer;

typedef struct LukeHttpRequest {
  LukeText method;
  LukeText path;
  LukeText query;
  LukeText body;
  LukeText last_event_id; /* SSE Last-Event-ID header (may be empty) */
  LukeText headers;       /* raw header block after request line */
  LukeText set_cookie;    /* pending Set-Cookie line(s); multiple separated by \n */
  LukeText user_id;       /* filled by luke_auth_require / login */
  LukeText csrf;          /* session CSRF (server-side; also on req after auth) */
  int client_fd;
  int keep_alive;         /* HTTP/1.1 keep-alive (unless Connection: close) */
  int replied;            /* httpReply / chunk / SSE already wrote */
  int streaming;          /* chunked or SSE — connection not reused after */
  char peer_ip[48];       /* direct peer; X-Forwarded-For if LUKE_TRUST_PROXY=1 */
} LukeHttpRequest;

static inline LukeText luke_http__dup(LukeArena *a, const char *s, size_t n) {
  char *p = (char *)luke_arena_alloc(a, n + 1, 1);
  if (n) memcpy(p, s, n);
  p[n] = '\0';
  return luke_text_n(p, n);
}

#if defined(__wasi__)

static inline LukeHttpServer *luke_http_listen(LukeArena *a, double port) {
  (void)a;
  (void)port;
  return NULL;
}

static inline LukeHttpRequest *luke_http_accept(LukeArena *a, LukeHttpServer *s) {
  (void)a;
  (void)s;
  return NULL;
}

static inline int luke_http_reply(LukeHttpRequest *req, double status, LukeText content_type,
                                  LukeText body) {
  (void)req;
  (void)status;
  (void)content_type;
  (void)body;
  return 0;
}

static inline int luke_http_sse_open(LukeHttpRequest *req) {
  (void)req;
  return 0;
}

static inline int luke_http_sse_data(LukeHttpRequest *req, LukeText data) {
  (void)req;
  (void)data;
  return 0;
}

static inline int luke_http_sse_id(LukeHttpRequest *req, LukeText id) {
  (void)req;
  (void)id;
  return 0;
}

static inline int luke_http_sse_comment(LukeHttpRequest *req, LukeText comment) {
  (void)req;
  (void)comment;
  return 0;
}

static inline int luke_http_close(LukeHttpRequest *req) {
  (void)req;
  return 0;
}

static inline int luke_http_serve(LukeHttpServer *s, void (*handler)(LukeArena *, LukeHttpRequest *),
                                  double max_conn) {
  (void)s;
  (void)handler;
  (void)max_conn;
  return 0;
}

static inline int luke_http_chunk_open(LukeHttpRequest *req, double status, LukeText content_type) {
  (void)req;
  (void)status;
  (void)content_type;
  return 0;
}

static inline int luke_http_chunk(LukeHttpRequest *req, LukeText data) {
  (void)req;
  (void)data;
  return 0;
}

static inline int luke_http_chunk_end(LukeHttpRequest *req) {
  (void)req;
  return 0;
}

static inline LukeText luke_http_client_ip(LukeArena *a, LukeHttpRequest *req) {
  (void)a;
  (void)req;
  return luke_text("");
}

#else /* !__wasi__ */

#ifndef LUKE_HTTP_TIMEOUT_MS
#define LUKE_HTTP_TIMEOUT_MS 10000
#endif
/* 0 = unlimited keep-alive requests per connection (env LUKE_HTTP_KEEPALIVE_MAX overrides). */
#ifndef LUKE_HTTP_KEEPALIVE_MAX
#define LUKE_HTTP_KEEPALIVE_MAX 100000
#endif
#ifndef LUKE_HTTP_ARENA_BYTES
#define LUKE_HTTP_ARENA_BYTES (8u << 10)
#endif
#ifndef LUKE_HTTP_BACKLOG
#define LUKE_HTTP_BACKLOG 512
#endif

static inline LukeHttpServer *luke_http_listen(LukeArena *a, double port) {
  int p = (int)port;
  if (p <= 0 || p > 65535) return NULL;

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return NULL;

  int yes = 1;
  (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
  (void)setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)p);

  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return NULL;
  }
  if (listen(fd, LUKE_HTTP_BACKLOG) < 0) {
    close(fd);
    return NULL;
  }

  LukeHttpServer *s = (LukeHttpServer *)luke_arena_alloc(a, sizeof(LukeHttpServer), 8);
  s->fd = fd;
  return s;
}

/* Find end of HTTP headers (\r\n\r\n). Returns offset of first body byte, or -1. */
static inline int luke_http__hdr_end(const char *buf, size_t n) {
  for (size_t i = 0; i + 3 < n; ++i) {
    if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
      return (int)(i + 4);
  }
  return -1;
}

static inline long luke_http__content_length(const char *hdrs, size_t hdr_len) {
  const char *key = "content-length:";
  size_t klen = 15;
  size_t i = 0;
  while (i + klen < hdr_len) {
    size_t j = 0;
    while (j < klen && i + j < hdr_len) {
      char c = hdrs[i + j];
      char e = key[j];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      if (c != e) break;
      ++j;
    }
    if (j == klen) {
      i += klen;
      while (i < hdr_len && (hdrs[i] == ' ' || hdrs[i] == '\t')) ++i;
      long v = 0;
      int any = 0;
      while (i < hdr_len && hdrs[i] >= '0' && hdrs[i] <= '9') {
        any = 1;
        v = v * 10 + (hdrs[i] - '0');
        ++i;
      }
      return any ? v : -1;
    }
    ++i;
  }
  return -1;
}

static inline int luke_http__ka_budget(void) {
  int n = LUKE_HTTP_KEEPALIVE_MAX;
  const char *e = getenv("LUKE_HTTP_KEEPALIVE_MAX");
  if (e && e[0]) n = atoi(e);
  if (n <= 0) return INT_MAX;
  return n;
}

static inline void luke_http__set_timeouts(int fd) {
  int ms = LUKE_HTTP_TIMEOUT_MS;
  const char *e = getenv("LUKE_HTTP_TIMEOUT_MS");
  if (e && e[0]) ms = atoi(e);
  if (ms <= 0) return;
  struct timeval tv;
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (long)(ms % 1000) * 1000L;
  (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static inline void luke_http__set_nodelay(int fd) {
  int one = 1;
  (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

static inline void luke_http__peer_ip(int cfd, char *out, size_t out_n) {
  if (!out || out_n == 0) return;
  out[0] = '\0';
  struct sockaddr_in peer;
  socklen_t plen = sizeof(peer);
  if (getpeername(cfd, (struct sockaddr *)&peer, &plen) == 0)
    inet_ntop(AF_INET, &peer.sin_addr, out, (socklen_t)out_n);
}

/* Connection: close → 0; Connection: keep-alive → 1; else HTTP/1.1 default 1, HTTP/1.0 default 0. */
static inline int luke_http__want_keepalive(const char *buf, size_t hdr_end, int http11) {
  int explicit_close = 0, explicit_ka = 0;
  size_t h = 0;
  while (h < hdr_end) {
    size_t line0 = h;
    while (h < hdr_end && buf[h] != '\n') ++h;
    size_t line1 = h;
    if (h < hdr_end) ++h;
    size_t L = line1 > line0 && buf[line1 - 1] == '\r' ? line1 - 1 : line1;
    if (L <= line0) continue;
    const char *key = "connection:";
    size_t klen = 11;
    if (L - line0 < klen) continue;
    int match = 1;
    for (size_t k = 0; k < klen; ++k) {
      char c = buf[line0 + k];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      if (c != key[k]) {
        match = 0;
        break;
      }
    }
    if (!match) continue;
    size_t v0 = line0 + klen;
    while (v0 < L && (buf[v0] == ' ' || buf[v0] == '\t')) ++v0;
    /* scan token list */
    for (size_t i = v0; i < L; ++i) {
      char c = buf[i];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      if ((c == 'c') && i + 4 < L) {
        int ok = 1;
        const char *w = "close";
        for (int j = 0; j < 5; ++j) {
          char d = buf[i + j];
          if (d >= 'A' && d <= 'Z') d = (char)(d - 'A' + 'a');
          if (d != w[j]) {
            ok = 0;
            break;
          }
        }
        if (ok) explicit_close = 1;
      }
      if ((c == 'k') && i + 9 < L) {
        int ok = 1;
        const char *w = "keep-alive";
        for (int j = 0; j < 10; ++j) {
          char d = buf[i + j];
          if (d >= 'A' && d <= 'Z') d = (char)(d - 'A' + 'a');
          if (d != w[j]) {
            ok = 0;
            break;
          }
        }
        if (ok) explicit_ka = 1;
      }
    }
  }
  if (explicit_close) return 0;
  if (explicit_ka) return 1;
  return http11 ? 1 : 0;
}

static inline void luke_http__apply_forwarded(LukeHttpRequest *req) {
  if (!req) return;
  const char *trust = getenv("LUKE_TRUST_PROXY");
  if (!trust || trust[0] != '1') return;
  /* First X-Forwarded-For hop */
  const char *buf = req->headers.ptr;
  size_t hdr_end = req->headers.len;
  if (!buf || !hdr_end) return;
  size_t h = 0;
  while (h < hdr_end) {
    size_t line0 = h;
    while (h < hdr_end && buf[h] != '\n') ++h;
    size_t line1 = h;
    if (h < hdr_end) ++h;
    size_t L = line1 > line0 && buf[line1 - 1] == '\r' ? line1 - 1 : line1;
    if (L <= line0) continue;
    const char *key = "x-forwarded-for:";
    size_t klen = 16;
    if (L - line0 < klen) continue;
    int match = 1;
    for (size_t k = 0; k < klen; ++k) {
      char c = buf[line0 + k];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      if (c != key[k]) {
        match = 0;
        break;
      }
    }
    if (!match) continue;
    size_t v0 = line0 + klen;
    while (v0 < L && (buf[v0] == ' ' || buf[v0] == '\t')) ++v0;
    size_t v1 = v0;
    while (v1 < L && buf[v1] != ',' && buf[v1] != ' ') ++v1;
    size_t n = v1 - v0;
    if (n == 0 || n >= sizeof(req->peer_ip)) return;
    memcpy(req->peer_ip, buf + v0, n);
    req->peer_ip[n] = '\0';
    return;
  }
}

/* 1 if buf holds a complete request (headers + Content-Length body). */
static inline int luke_http__request_ready(const char *buf, size_t len) {
  int hdr_end = luke_http__hdr_end(buf, len);
  if (hdr_end < 0) return len > (1u << 20) ? -1 : 0;
  long need_body = luke_http__content_length(buf, (size_t)hdr_end);
  if (need_body < 0) need_body = 0;
  if (need_body > (1L << 20)) return -1;
  return (long)(len - (size_t)hdr_end) >= need_body ? 1 : 0;
}

/* Parse a complete request already in buf[0,len). Consumes one message; *used is bytes eaten. */
static inline LukeHttpRequest *luke_http_parse_complete(LukeArena *a, const char *buf, size_t len,
                                                        int cfd, size_t *used) {
  if (!a || !buf) return NULL;
  int hdr_end = luke_http__hdr_end(buf, len);
  if (hdr_end < 0) return NULL;
  long need_body = luke_http__content_length(buf, (size_t)hdr_end);
  if (need_body < 0) need_body = 0;
  if ((long)(len - (size_t)hdr_end) < need_body) return NULL;
  if (used) *used = (size_t)hdr_end + (size_t)need_body;

  /* Request line: METHOD SP path[?query] SP HTTP/x.y */
  size_t i = 0;
  while (i < (size_t)hdr_end && buf[i] != ' ' && buf[i] != '\r' && buf[i] != '\n') ++i;
  LukeText method = luke_http__dup(a, buf, i);
  while (i < (size_t)hdr_end && buf[i] == ' ') ++i;
  size_t path_start = i;
  while (i < (size_t)hdr_end && buf[i] != ' ' && buf[i] != '?' && buf[i] != '\r' &&
         buf[i] != '\n')
    ++i;
  LukeText path = luke_http__dup(a, buf + path_start, i - path_start);
  LukeText query = luke_text("");
  if (i < (size_t)hdr_end && buf[i] == '?') {
    ++i;
    size_t q0 = i;
    while (i < (size_t)hdr_end && buf[i] != ' ' && buf[i] != '\r' && buf[i] != '\n') ++i;
    query = luke_http__dup(a, buf + q0, i - q0);
  }
  while (i < (size_t)hdr_end && buf[i] == ' ') ++i;
  int http11 = 1;
  if (i + 8 <= (size_t)hdr_end && memcmp(buf + i, "HTTP/1.0", 8) == 0) http11 = 0;

  LukeText body = luke_text("");
  if (need_body > 0) body = luke_http__dup(a, buf + hdr_end, (size_t)need_body);

  LukeText last_event_id = luke_text("");
  LukeText headers = luke_text("");
  {
    size_t line_end = 0;
    while (line_end < (size_t)hdr_end && buf[line_end] != '\n') ++line_end;
    if (line_end < (size_t)hdr_end) ++line_end;
    if ((size_t)hdr_end > line_end)
      headers = luke_http__dup(a, buf + line_end, (size_t)hdr_end - line_end);

    size_t h = 0;
    while (h < (size_t)hdr_end) {
      size_t line0 = h;
      while (h < (size_t)hdr_end && buf[h] != '\n') ++h;
      size_t line1 = h;
      if (h < (size_t)hdr_end) ++h;
      if (line0 == 0) continue;
      size_t L = line1 > line0 && buf[line1 - 1] == '\r' ? line1 - 1 : line1;
      if (L <= line0) continue;
      const char *key = "last-event-id:";
      size_t klen = 14;
      if (L - line0 < klen) continue;
      int match = 1;
      for (size_t k = 0; k < klen; ++k) {
        char c = buf[line0 + k];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != key[k]) {
          match = 0;
          break;
        }
      }
      if (!match) continue;
      size_t v0 = line0 + klen;
      while (v0 < L && (buf[v0] == ' ' || buf[v0] == '\t')) ++v0;
      last_event_id = luke_http__dup(a, buf + v0, L - v0);
      break;
    }
  }

  int keep_alive = luke_http__want_keepalive(buf, (size_t)hdr_end, http11);

  LukeHttpRequest *req = (LukeHttpRequest *)luke_arena_alloc(a, sizeof(LukeHttpRequest), 8);
  memset(req, 0, sizeof(*req));
  req->method = method;
  req->path = path;
  req->query = query;
  req->body = body;
  req->last_event_id = last_event_id;
  req->headers = headers;
  req->set_cookie = luke_text("");
  req->user_id = luke_text("");
  req->csrf = luke_text("");
  req->client_fd = cfd;
  req->keep_alive = keep_alive;
  req->replied = 0;
  req->streaming = 0;
  luke_http__peer_ip(cfd, req->peer_ip, sizeof(req->peer_ip));
  luke_http__apply_forwarded(req);
  return req;
}

/* Blocking read of one HTTP/1.1 request (legacy / LUKE_HTTP_IO=pool). */
static inline LukeHttpRequest *luke_http_read_request(LukeArena *a, int cfd) {
  if (!a || cfd < 0) return NULL;
  size_t cap = 4096, n = 0;
  char *buf = (char *)malloc(cap);
  if (!buf) return NULL;
  for (;;) {
    if (n + 2048 > cap) {
      cap *= 2;
      char *nb = (char *)realloc(buf, cap);
      if (!nb) {
        free(buf);
        return NULL;
      }
      buf = nb;
    }
    ssize_t r = recv(cfd, buf + n, cap - n, 0);
    if (r <= 0) {
      free(buf);
      return NULL;
    }
    n += (size_t)r;
    int ready = luke_http__request_ready(buf, n);
    if (ready < 0) {
      free(buf);
      return NULL;
    }
    if (ready) break;
  }
  LukeHttpRequest *req = luke_http_parse_complete(a, buf, n, cfd, NULL);
  free(buf);
  return req;
}

static inline LukeHttpRequest *luke_http_accept(LukeArena *a, LukeHttpServer *s) {
  if (!s || s->fd < 0) return NULL;
  int cfd = accept(s->fd, NULL, NULL);
  if (cfd < 0) return NULL;
  luke_http__set_timeouts(cfd);
  LukeHttpRequest *req = luke_http_read_request(a, cfd);
  if (!req) {
    close(cfd);
    return NULL;
  }
  return req;
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

enum {
  LUKE_HTTP_ST_READ = 0,
  LUKE_HTTP_ST_HANDLE,
  LUKE_HTTP_ST_WRITE,
  LUKE_HTTP_ST_STREAM
};

typedef struct LukeHttpLoop LukeHttpLoop;
typedef struct LukeHttpConn LukeHttpConn;

typedef void (*LukeHttpHandler)(LukeArena *arena, LukeHttpRequest *req);

typedef struct LukeHttpServeJob {
  LukeHttpHandler handler;
  int client_fd;      /* LUKE_HTTP_IO=pool: worker owns blocking recv */
  LukeHttpConn *conn; /* event mode: request already buffered */
  struct LukeHttpServeJob *free_next;
} LukeHttpServeJob;

typedef struct LukeHttpConn {
  int fd;
  int state;
  int close_after_write;
  int keep_alive;
  int ka_left;
  int pending_handle;
  int interest; /* bit0 = in, bit1 = out */
  uint64_t last_ms;
  char *in;
  size_t in_len, in_cap;
  char *out;
  size_t out_len, out_off, out_cap;
  LukeHttpLoop *loop;
  LukeHttpServeJob job; /* embedded — no malloc on the event hot path */
  struct LukeHttpConn *live_next;
  struct LukeHttpConn *live_prev;
  struct LukeHttpConn *done_next;
} LukeHttpConn;

static __thread LukeHttpConn *luke_http__cur_conn = NULL;

static inline int luke_http__out_append(LukeHttpConn *c, const char *buf, size_t len) {
  if (!c || !buf) return 0;
  if (!len) return 1;
  size_t need = c->out_len + len;
  if (need > (4u << 20)) return 0;
  if (need > c->out_cap) {
    size_t cap = c->out_cap ? c->out_cap : 4096;
    while (cap < need) cap *= 2;
    char *nb = (char *)realloc(c->out, cap);
    if (!nb) return 0;
    c->out = nb;
    c->out_cap = cap;
  }
  memcpy(c->out + c->out_len, buf, len);
  c->out_len += len;
  return 1;
}

static inline int luke_http__send_all_block(int fd, const char *buf, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
    if (n <= 0) return 0;
    sent += (size_t)n;
  }
  return 1;
}

static inline int luke_http__send_all(int fd, const char *buf, size_t len) {
  LukeHttpConn *c = luke_http__cur_conn;
  if (c && c->fd == fd && c->state != LUKE_HTTP_ST_STREAM) return luke_http__out_append(c, buf, len);
  return luke_http__send_all_block(fd, buf, len);
}

static inline void luke_http__take_stream(LukeHttpRequest *req) {
  LukeHttpConn *c = luke_http__cur_conn;
  if (!c) return;
  luke_http__cur_conn = NULL;
  c->state = LUKE_HTTP_ST_STREAM;
  if (c->out_len > c->out_off) {
    int ok = luke_http__send_all_block(c->fd, c->out + c->out_off, c->out_len - c->out_off);
    c->out_off = c->out_len = 0;
    if (!ok) {
      if (c->fd >= 0) close(c->fd);
      c->fd = -1;
      if (req) req->client_fd = -1;
    }
  }
}

static inline int luke_http_reply(LukeHttpRequest *req, double status, LukeText content_type,
                                  LukeText body) {
  if (!req || req->client_fd < 0) return 0;
  int code = (int)status;
  if (code < 100 || code > 599) code = 500;

  const char *reason = "OK";
  if (code == 201)
    reason = "Created";
  else if (code == 204)
    reason = "No Content";
  else if (code == 400)
    reason = "Bad Request";
  else if (code == 401)
    reason = "Unauthorized";
  else if (code == 403)
    reason = "Forbidden";
  else if (code == 404)
    reason = "Not Found";
  else if (code == 405)
    reason = "Method Not Allowed";
  else if (code == 500)
    reason = "Internal Server Error";
  else if (code != 200)
    reason = "OK";

  char ctype[256];
  size_t ct_n = content_type.len < sizeof(ctype) - 1 ? content_type.len : sizeof(ctype) - 1;
  if (ct_n == 0) {
    memcpy(ctype, "text/plain; charset=utf-8", 25);
    ct_n = 25;
  } else {
    memcpy(ctype, content_type.ptr, ct_n);
  }
  ctype[ct_n] = '\0';

  int ka = req->keep_alive && !req->streaming;
  char hdr[2048];
  int hlen = snprintf(hdr, sizeof(hdr),
                      "HTTP/1.1 %d %s\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %zu\r\n",
                      code, reason, ctype, body.len);
  if (hlen < 0 || (size_t)hlen >= sizeof(hdr)) {
    close(req->client_fd);
    req->client_fd = -1;
    return 0;
  }
  if (req->set_cookie.len > 0 && req->set_cookie.ptr) {
    size_t i = 0;
    while (i < req->set_cookie.len) {
      size_t line0 = i;
      while (i < req->set_cookie.len && req->set_cookie.ptr[i] != '\n') ++i;
      size_t line_n = i - line0;
      if (i < req->set_cookie.len) ++i;
      if (line_n == 0) continue;
      int add = snprintf(hdr + hlen, sizeof(hdr) - (size_t)hlen, "Set-Cookie: %.*s\r\n",
                         (int)line_n, req->set_cookie.ptr + line0);
      if (add < 0 || (size_t)hlen + (size_t)add >= sizeof(hdr)) {
        close(req->client_fd);
        req->client_fd = -1;
        return 0;
      }
      hlen += add;
    }
  }
  {
    int add = snprintf(hdr + hlen, sizeof(hdr) - (size_t)hlen,
                       "Access-Control-Allow-Origin: *\r\n"
                       "Connection: %s\r\n"
                       "\r\n",
                       ka ? "keep-alive" : "close");
    if (add < 0 || (size_t)hlen + (size_t)add >= sizeof(hdr)) {
      close(req->client_fd);
      req->client_fd = -1;
      return 0;
    }
    hlen += add;
  }

  {
    struct iovec iov[2];
    int niov = 0;
    iov[niov].iov_base = (void *)hdr;
    iov[niov].iov_len = (size_t)hlen;
    niov++;
    if (body.len && body.ptr) {
      iov[niov].iov_base = (void *)body.ptr;
      iov[niov].iov_len = body.len;
      niov++;
    }
    LukeHttpConn *c = luke_http__cur_conn;
    int buffered = c && c->fd == req->client_fd && c->state != LUKE_HTTP_ST_STREAM;
    while (niov > 0) {
      ssize_t n = writev(req->client_fd, iov, niov);
      if (n < 0) {
        if (errno == EINTR) continue;
        if (buffered && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        close(req->client_fd);
        req->client_fd = -1;
        return 0;
      }
      if (n == 0) {
        if (buffered) break;
        close(req->client_fd);
        req->client_fd = -1;
        return 0;
      }
      size_t left = (size_t)n;
      while (niov > 0 && left >= iov[0].iov_len) {
        left -= iov[0].iov_len;
        --niov;
        if (niov > 0) iov[0] = iov[1];
      }
      if (niov > 0 && left > 0) {
        iov[0].iov_base = (char *)iov[0].iov_base + left;
        iov[0].iov_len -= left;
      }
    }
    while (niov > 0) {
      if (!buffered || !luke_http__out_append(c, (const char *)iov[0].iov_base, iov[0].iov_len)) {
        close(req->client_fd);
        req->client_fd = -1;
        return 0;
      }
      --niov;
      if (niov > 0) iov[0] = iov[1];
    }
  }

  req->replied = 1;
  if (!ka) {
    if (luke_http__cur_conn) {
      luke_http__cur_conn->close_after_write = 1;
    } else {
      close(req->client_fd);
      req->client_fd = -1;
    }
  }
  return 1;
}

/* ---------- SSE (Server-Sent Events) — keep connection open ---------- */

static inline int luke_http_sse_open(LukeHttpRequest *req) {
  if (!req || req->client_fd < 0) return 0;
  req->streaming = 1;
  req->keep_alive = 0;
  luke_http__take_stream(req);
  if (!req || req->client_fd < 0) return 0;
  const char *origin = getenv("LUKE_SSE_ORIGIN");
  if (!origin || !origin[0]) origin = "*";
  char hdr[512];
  snprintf(hdr, sizeof(hdr),
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/event-stream\r\n"
           "Cache-Control: no-cache\r\n"
           "Access-Control-Allow-Origin: %s\r\n"
           "Connection: keep-alive\r\n"
           "\r\n",
           origin);
  if (!luke_http__send_all(req->client_fd, hdr, strlen(hdr))) {
    close(req->client_fd);
    req->client_fd = -1;
    return 0;
  }
  if (!luke_http__send_all(req->client_fd, ": luke-sse\n\n", 12)) {
    close(req->client_fd);
    req->client_fd = -1;
    return 0;
  }
  req->replied = 1;
  return 1;
}

static inline int luke_http_sse_data(LukeHttpRequest *req, LukeText data) {
  if (!req || req->client_fd < 0) return 0;
  if (!luke_http__send_all(req->client_fd, "data: ", 6)) goto fail;
  if (data.len && data.ptr) {
    size_t i = 0;
    while (i < data.len) {
      size_t j = i;
      while (j < data.len && data.ptr[j] != '\n' && data.ptr[j] != '\r') ++j;
      if (j > i) {
        if (!luke_http__send_all(req->client_fd, data.ptr + i, j - i)) goto fail;
      }
      i = j;
      if (i < data.len && data.ptr[i] == '\r') ++i;
      if (i < data.len && data.ptr[i] == '\n') {
        ++i;
        if (i < data.len) {
          if (!luke_http__send_all(req->client_fd, "\ndata: ", 7)) goto fail;
        }
      }
    }
  }
  if (!luke_http__send_all(req->client_fd, "\n\n", 2)) goto fail;
  return 1;
fail:
  close(req->client_fd);
  req->client_fd = -1;
  return 0;
}

static inline int luke_http_sse_id(LukeHttpRequest *req, LukeText id) {
  if (!req || req->client_fd < 0) return 0;
  if (!luke_http__send_all(req->client_fd, "id: ", 4)) goto fail;
  if (id.len && id.ptr) {
    if (!luke_http__send_all(req->client_fd, id.ptr, id.len)) goto fail;
  }
  if (!luke_http__send_all(req->client_fd, "\n", 1)) goto fail;
  return 1;
fail:
  close(req->client_fd);
  req->client_fd = -1;
  return 0;
}

static inline int luke_http_sse_comment(LukeHttpRequest *req, LukeText comment) {
  if (!req || req->client_fd < 0) return 0;
  if (!luke_http__send_all(req->client_fd, ": ", 2)) goto fail;
  if (comment.len && comment.ptr) {
    if (!luke_http__send_all(req->client_fd, comment.ptr, comment.len)) goto fail;
  }
  if (!luke_http__send_all(req->client_fd, "\n\n", 2)) goto fail;
  return 1;
fail:
  close(req->client_fd);
  req->client_fd = -1;
  return 0;
}

static inline int luke_http_close(LukeHttpRequest *req) {
  if (!req || req->client_fd < 0) return 0;
  close(req->client_fd);
  req->client_fd = -1;
  return 1;
}

/* ---------- Chunked transfer (response streaming beachhead) ---------- */

static inline int luke_http_chunk_open(LukeHttpRequest *req, double status, LukeText content_type) {
  if (!req || req->client_fd < 0) return 0;
  req->streaming = 1;
  req->keep_alive = 0;
  luke_http__take_stream(req);
  if (!req || req->client_fd < 0) return 0;
  int code = (int)status;
  if (code < 100 || code > 599) code = 200;
  char ctype[256];
  size_t ct_n = content_type.len < sizeof(ctype) - 1 ? content_type.len : sizeof(ctype) - 1;
  if (ct_n == 0) {
    memcpy(ctype, "text/plain; charset=utf-8", 25);
    ct_n = 25;
  } else {
    memcpy(ctype, content_type.ptr, ct_n);
  }
  ctype[ct_n] = '\0';
  char hdr[512];
  int hlen = snprintf(hdr, sizeof(hdr),
                      "HTTP/1.1 %d OK\r\n"
                      "Content-Type: %s\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      code, ctype);
  if (hlen < 0 || !luke_http__send_all(req->client_fd, hdr, (size_t)hlen)) {
    close(req->client_fd);
    req->client_fd = -1;
    return 0;
  }
  req->streaming = 1;
  req->keep_alive = 0;
  req->replied = 1;
  return 1;
}

static inline int luke_http_chunk(LukeHttpRequest *req, LukeText data) {
  if (!req || req->client_fd < 0) return 0;
  if (!data.len) return 1;
  char line[32];
  int n = snprintf(line, sizeof(line), "%zx\r\n", data.len);
  if (n < 0 || !luke_http__send_all(req->client_fd, line, (size_t)n)) goto fail;
  if (!luke_http__send_all(req->client_fd, data.ptr, data.len)) goto fail;
  if (!luke_http__send_all(req->client_fd, "\r\n", 2)) goto fail;
  return 1;
fail:
  close(req->client_fd);
  req->client_fd = -1;
  return 0;
}

static inline int luke_http_chunk_end(LukeHttpRequest *req) {
  if (!req || req->client_fd < 0) return 0;
  if (!luke_http__send_all(req->client_fd, "0\r\n\r\n", 5)) {
    close(req->client_fd);
    req->client_fd = -1;
    return 0;
  }
  close(req->client_fd);
  req->client_fd = -1;
  return 1;
}

static inline LukeText luke_http_client_ip(LukeArena *a, LukeHttpRequest *req) {
  (void)a;
  if (!req || !req->peer_ip[0]) return luke_text("");
  return luke_text(req->peer_ip);
}

/* ---------- Concurrent serve (event-loop I/O + handler pool) ---------- */

#ifndef LUKE_HTTP_POOL_WORKERS
#define LUKE_HTTP_POOL_WORKERS 8
#endif
#ifndef LUKE_HTTP_POOL_QUEUE
#define LUKE_HTTP_POOL_QUEUE 64
#endif

#define LUKE_HTTP_EV_LISTEN ((void *)(intptr_t)1)
#define LUKE_HTTP_EV_WAKE ((void *)(intptr_t)2)

typedef struct LukeHttpArenaNode {
  LukeArena arena; /* must be first — release casts LukeArena* back */
  struct LukeHttpArenaNode *next;
} LukeHttpArenaNode;

static __thread LukeHttpArenaNode *luke_http__arena_tls = NULL;

static inline LukeArena *luke_http__arena_acquire(void) {
  LukeHttpArenaNode *n = luke_http__arena_tls;
  if (n) {
    luke_http__arena_tls = n->next;
    n->next = NULL;
    luke_arena_clear(&n->arena);
    return &n->arena;
  }
  n = (LukeHttpArenaNode *)calloc(1, sizeof(LukeHttpArenaNode));
  if (!n) return NULL;
  luke_arena_init(&n->arena, LUKE_HTTP_ARENA_BYTES);
  return &n->arena;
}

static inline void luke_http__arena_release(LukeArena *a) {
  if (!a) return;
  LukeHttpArenaNode *n = (LukeHttpArenaNode *)a;
  luke_arena_clear(&n->arena);
  n->next = luke_http__arena_tls;
  luke_http__arena_tls = n;
}

typedef struct LukeHttpPool {
  pthread_mutex_t mu;
  pthread_cond_t not_empty;
  pthread_cond_t not_full;
  LukeHttpServeJob *q[LUKE_HTTP_POOL_QUEUE];
  int head;
  int len;
  int stop;
  int nworkers;
  LukeHttpHandler handler;
  LukeHttpLoop *loop;
  LukeHttpServeJob *job_free; /* pool-I/O path only */
} LukeHttpPool;

struct LukeHttpLoop {
  int evfd;
  int kind; /* 1=epoll, 2=kqueue, 0=poll */
  int wake[2];
  int listen_fd;
  int listen_on;
  int own_listen; /* 1 if this loop created listen_fd (REUSEPORT shard) */
  int inline_handlers;
  LukeHttpConn **by_fd;
  int by_fd_cap;
  LukeHttpConn *live;
  int live_n;
  int in_flight;
  LukeHttpPool *pool;
  LukeHttpHandler handler;
  pthread_mutex_t done_mu;
  LukeHttpConn *done_head;
  struct pollfd *pfds;
  int pfd_cap;
  /* Shared accept budget across REUSEPORT loops (NULL = local only). */
  pthread_mutex_t *budget_mu;
  int *budget_left;
  int *budget_started;
  int unlimited;
};

static volatile sig_atomic_t luke_http__stop_flag = 0;

static void luke_http__on_signal(int sig) {
  (void)sig;
  luke_http__stop_flag = 1;
}

static inline uint64_t luke_http__now_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static inline int luke_http__timeout_ms(void) {
  int ms = LUKE_HTTP_TIMEOUT_MS;
  const char *e = getenv("LUKE_HTTP_TIMEOUT_MS");
  if (e && e[0]) ms = atoi(e);
  return ms;
}

static inline int luke_http__set_nb(int fd) {
  int fl = fcntl(fd, F_GETFL, 0);
  if (fl < 0) return -1;
  return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static inline int luke_http__want_pool_io(void) {
  const char *e = getenv("LUKE_HTTP_IO");
  return e && (strcmp(e, "pool") == 0 || strcmp(e, "thread") == 0);
}

static inline int luke_http__want_inline(void) {
  const char *e = getenv("LUKE_HTTP_INLINE");
  return e && e[0] == '1';
}

static inline int luke_http__nloops(void) {
  const char *e = getenv("LUKE_HTTP_LOOPS");
  if (e && e[0]) {
    int n = atoi(e);
    if (n < 1) return 1;
    if (n > 64) return 64;
    return n;
  }
#ifdef SO_REUSEPORT
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  if (n < 1) n = 1;
  if (n > 64) n = 64;
  return (int)n;
#else
  return 1;
#endif
}

static inline int luke_http__workers_per_loop(int nloops) {
  int w = LUKE_HTTP_POOL_WORKERS;
  const char *e = getenv("LUKE_HTTP_POOL_WORKERS");
  if (e && e[0]) w = atoi(e);
  if (w < 0) w = 0;
  if (luke_http__want_inline()) return 0;
  if (w == 0) return 0;
  if (nloops <= 1) return w;
  /* Spread the configured pool across loops; at least 1 when offloading. */
  int per = w / nloops;
  return per < 1 ? 1 : per;
}

static inline void luke_http__conn_free(LukeHttpConn *c) {
  if (!c) return;
  free(c->in);
  free(c->out);
  free(c);
}

static inline void luke_http__live_link(LukeHttpLoop *loop, LukeHttpConn *c) {
  c->live_prev = NULL;
  c->live_next = loop->live;
  if (loop->live) loop->live->live_prev = c;
  loop->live = c;
  loop->live_n++;
}

static inline void luke_http__live_unlink(LukeHttpLoop *loop, LukeHttpConn *c) {
  if (c->live_prev)
    c->live_prev->live_next = c->live_next;
  else if (loop->live == c)
    loop->live = c->live_next;
  if (c->live_next) c->live_next->live_prev = c->live_prev;
  c->live_next = c->live_prev = NULL;
  if (loop->live_n > 0) loop->live_n--;
}

static inline void luke_http__reg_fd(LukeHttpLoop *loop, LukeHttpConn *c) {
  if (!c || c->fd < 0) return;
  if (c->fd >= loop->by_fd_cap) {
    int ncap = loop->by_fd_cap ? loop->by_fd_cap : 64;
    while (ncap <= c->fd) ncap *= 2;
    LukeHttpConn **n = (LukeHttpConn **)realloc(loop->by_fd, (size_t)ncap * sizeof(*n));
    if (!n) return;
    memset(n + loop->by_fd_cap, 0, (size_t)(ncap - loop->by_fd_cap) * sizeof(*n));
    loop->by_fd = n;
    loop->by_fd_cap = ncap;
  }
  loop->by_fd[c->fd] = c;
}

static inline void luke_http__unreg_fd(LukeHttpLoop *loop, LukeHttpConn *c) {
  if (!c || c->fd < 0 || c->fd >= loop->by_fd_cap) return;
  if (loop->by_fd[c->fd] == c) loop->by_fd[c->fd] = NULL;
}

static inline int luke_http__ev_open(LukeHttpLoop *loop) {
  loop->evfd = -1;
  loop->kind = 0;
#if defined(__linux__)
  loop->evfd = epoll_create1(EPOLL_CLOEXEC);
  if (loop->evfd < 0) loop->evfd = epoll_create(64);
  if (loop->evfd >= 0) loop->kind = 1;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
  loop->evfd = kqueue();
  if (loop->evfd >= 0) loop->kind = 2;
#endif
  return 0;
}

static inline int luke_http__ev_add_special(LukeHttpLoop *loop, int fd, void *who) {
  if (loop->kind == 1) {
#if defined(__linux__)
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.ptr = who;
    return epoll_ctl(loop->evfd, EPOLL_CTL_ADD, fd, &ev);
#else
    (void)fd;
    (void)who;
    return 0;
#endif
  }
  if (loop->kind == 2) {
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    struct kevent kev;
    EV_SET(&kev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, who);
    return kevent(loop->evfd, &kev, 1, NULL, 0, NULL);
#else
    (void)fd;
    (void)who;
    return 0;
#endif
  }
  return 0;
}

static inline int luke_http__ev_del_fd(LukeHttpLoop *loop, int fd) {
  if (fd < 0) return 0;
  if (loop->kind == 1) {
#if defined(__linux__)
    return epoll_ctl(loop->evfd, EPOLL_CTL_DEL, fd, NULL);
#else
    return 0;
#endif
  }
  if (loop->kind == 2) {
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    struct kevent kev;
    EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    (void)kevent(loop->evfd, &kev, 1, NULL, 0, NULL);
    EV_SET(&kev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    (void)kevent(loop->evfd, &kev, 1, NULL, 0, NULL);
    return 0;
#else
    return 0;
#endif
  }
  return 0;
}

static inline int luke_http__ev_mod(LukeHttpLoop *loop, LukeHttpConn *c, int interest) {
  if (!c || c->fd < 0) return 0;
  c->interest = interest;
  if (loop->kind == 1) {
#if defined(__linux__)
    if (interest == 0) return epoll_ctl(loop->evfd, EPOLL_CTL_DEL, c->fd, NULL);
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.ptr = c;
    if (interest & 1) ev.events |= EPOLLIN;
    if (interest & 2) ev.events |= EPOLLOUT;
#ifdef EPOLLET
    ev.events |= EPOLLET;
#endif
#ifdef EPOLLRDHUP
    if (interest & 1) ev.events |= EPOLLRDHUP;
#endif
    if (epoll_ctl(loop->evfd, EPOLL_CTL_MOD, c->fd, &ev) < 0)
      return epoll_ctl(loop->evfd, EPOLL_CTL_ADD, c->fd, &ev);
    return 0;
#else
    return 0;
#endif
  }
  if (loop->kind == 2) {
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    struct kevent kev[2];
    int n = 0;
    if (interest & 1) {
      EV_SET(&kev[n++], c->fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, c);
    } else {
      EV_SET(&kev[n++], c->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    }
    if (interest & 2) {
      EV_SET(&kev[n++], c->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, c);
    } else {
      EV_SET(&kev[n++], c->fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    }
    (void)kevent(loop->evfd, kev, n, NULL, 0, NULL);
    return 0;
#else
    return 0;
#endif
  }
  return 0;
}

typedef struct LukeHttpEvHit {
  void *who;
  int ev; /* bit0 in, bit1 out, bit2 err */
} LukeHttpEvHit;

static inline int luke_http__ev_wait(LukeHttpLoop *loop, LukeHttpEvHit *hits, int max_hits,
                                     int timeout_ms) {
  if (loop->kind == 1) {
#if defined(__linux__)
    struct epoll_event evs[64];
    int ncap = max_hits < 64 ? max_hits : 64;
    int n = epoll_wait(loop->evfd, evs, ncap, timeout_ms);
    if (n < 0) return errno == EINTR ? 0 : -1;
    for (int i = 0; i < n; ++i) {
      hits[i].who = evs[i].data.ptr;
      hits[i].ev = 0;
      if (evs[i].events & EPOLLIN) hits[i].ev |= 1;
      if (evs[i].events & EPOLLOUT) hits[i].ev |= 2;
      if (evs[i].events & (EPOLLERR | EPOLLHUP)) hits[i].ev |= 4;
#ifdef EPOLLRDHUP
      if (evs[i].events & EPOLLRDHUP) hits[i].ev |= 4;
#endif
    }
    return n;
#else
    (void)hits;
    (void)max_hits;
    (void)timeout_ms;
    return 0;
#endif
  }
  if (loop->kind == 2) {
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    struct kevent evs[64];
    int ncap = max_hits < 64 ? max_hits : 64;
    struct timespec ts;
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
    int n = kevent(loop->evfd, NULL, 0, evs, ncap, timeout_ms < 0 ? NULL : &ts);
    if (n < 0) return errno == EINTR ? 0 : -1;
    for (int i = 0; i < n; ++i) {
      hits[i].who = evs[i].udata;
      hits[i].ev = 0;
      if (evs[i].filter == EVFILT_READ) hits[i].ev |= 1;
      if (evs[i].filter == EVFILT_WRITE) hits[i].ev |= 2;
      if (evs[i].flags & (EV_EOF | EV_ERROR)) hits[i].ev |= 4;
    }
    return n;
#else
    (void)hits;
    (void)max_hits;
    (void)timeout_ms;
    return 0;
#endif
  }

  int need = 2 + loop->live_n;
  if (need > loop->pfd_cap) {
    int ncap = loop->pfd_cap ? loop->pfd_cap : 16;
    while (ncap < need) ncap *= 2;
    struct pollfd *n = (struct pollfd *)realloc(loop->pfds, (size_t)ncap * sizeof(*n));
    if (!n) return -1;
    loop->pfds = n;
    loop->pfd_cap = ncap;
  }
  loop->pfds[0].fd = loop->listen_on ? loop->listen_fd : -1;
  loop->pfds[0].events = POLLIN;
  loop->pfds[0].revents = 0;
  loop->pfds[1].fd = loop->wake[0];
  loop->pfds[1].events = POLLIN;
  loop->pfds[1].revents = 0;
  int np = 2;
  for (LukeHttpConn *c = loop->live; c; c = c->live_next) {
    if (!c->interest || c->fd < 0) continue;
    loop->pfds[np].fd = c->fd;
    loop->pfds[np].events = 0;
    if (c->interest & 1) loop->pfds[np].events |= POLLIN;
    if (c->interest & 2) loop->pfds[np].events |= POLLOUT;
    loop->pfds[np].revents = 0;
    np++;
  }
  int pr = poll(loop->pfds, (nfds_t)np, timeout_ms);
  if (pr < 0) return errno == EINTR ? 0 : -1;
  if (pr == 0) return 0;
  int out = 0;
  if (loop->pfds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
    hits[out].who = LUKE_HTTP_EV_LISTEN;
    hits[out].ev = (loop->pfds[0].revents & POLLIN) ? 1 : 4;
    out++;
  }
  if (out < max_hits && (loop->pfds[1].revents & (POLLIN | POLLERR | POLLHUP))) {
    hits[out].who = LUKE_HTTP_EV_WAKE;
    hits[out].ev = 1;
    out++;
  }
  for (int i = 2; i < np && out < max_hits; ++i) {
    if (!loop->pfds[i].revents) continue;
    int fd = loop->pfds[i].fd;
    LukeHttpConn *c = (fd >= 0 && fd < loop->by_fd_cap) ? loop->by_fd[fd] : NULL;
    if (!c) continue;
    hits[out].who = c;
    hits[out].ev = 0;
    if (loop->pfds[i].revents & POLLIN) hits[out].ev |= 1;
    if (loop->pfds[i].revents & POLLOUT) hits[out].ev |= 2;
    if (loop->pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) hits[out].ev |= 4;
    out++;
  }
  return out;
}

static inline void luke_http__done_push(LukeHttpLoop *loop, LukeHttpConn *c) {
  pthread_mutex_lock(&loop->done_mu);
  c->done_next = loop->done_head;
  loop->done_head = c;
  pthread_mutex_unlock(&loop->done_mu);
  char x = 1;
  ssize_t w;
  do {
    w = write(loop->wake[1], &x, 1);
  } while (w < 0 && errno == EINTR);
}

static inline void luke_http__wake_eat(LukeHttpLoop *loop) {
  char buf[256];
  ssize_t r;
  do {
    r = read(loop->wake[0], buf, sizeof(buf));
  } while (r > 0 || (r < 0 && errno == EINTR));
}

static inline int luke_http__in_grow(LukeHttpConn *c, size_t add) {
  size_t need = c->in_len + add;
  if (need > (1u << 20)) return 0;
  if (need <= c->in_cap) return 1;
  size_t cap = c->in_cap ? c->in_cap : 4096;
  while (cap < need) cap *= 2;
  char *nb = (char *)realloc(c->in, cap);
  if (!nb) return 0;
  c->in = nb;
  c->in_cap = cap;
  return 1;
}

static inline int luke_http__nb_read(LukeHttpConn *c) {
  for (;;) {
    if (!luke_http__in_grow(c, 2048)) return -1;
    ssize_t r = recv(c->fd, c->in + c->in_len, c->in_cap - c->in_len, 0);
    if (r < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
      if (errno == EINTR) continue;
      return -1;
    }
    if (r == 0) return -1;
    c->in_len += (size_t)r;
    int ready = luke_http__request_ready(c->in, c->in_len);
    if (ready < 0) return -1;
    if (ready) return 1;
  }
}

static inline int luke_http__nb_flush(LukeHttpConn *c) {
  while (c->out_off < c->out_len) {
    ssize_t n = send(c->fd, c->out + c->out_off, c->out_len - c->out_off, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
      if (errno == EINTR) continue;
      return -1;
    }
    if (n == 0) return -1;
    c->out_off += (size_t)n;
  }
  c->out_off = c->out_len = 0;
  return 1;
}

static inline void luke_http__conn_close(LukeHttpLoop *loop, LukeHttpConn *c) {
  if (!c) return;
  luke_http__ev_mod(loop, c, 0);
  luke_http__unreg_fd(loop, c);
  luke_http__live_unlink(loop, c);
  if (c->fd >= 0) {
    close(c->fd);
    c->fd = -1;
  }
  luke_http__conn_free(c);
}

static inline int luke_http__try_enqueue(LukeHttpLoop *loop, LukeHttpConn *c);

static inline void luke_http__after_write(LukeHttpLoop *loop, LukeHttpConn *c) {
  c->out_len = c->out_off = 0;
  if (c->close_after_write || !c->keep_alive || c->ka_left <= 0) {
    luke_http__conn_close(loop, c);
    return;
  }
  c->state = LUKE_HTTP_ST_READ;
  c->close_after_write = 0;
  c->last_ms = luke_http__now_ms();
  if (c->in_len) {
    int ready = luke_http__request_ready(c->in, c->in_len);
    if (ready < 0) {
      luke_http__conn_close(loop, c);
      return;
    }
    if (ready > 0) {
      (void)luke_http__try_enqueue(loop, c);
      return;
    }
  }
  luke_http__ev_mod(loop, c, 1);
}

static inline void luke_http__drain_done(LukeHttpLoop *loop) {
  pthread_mutex_lock(&loop->done_mu);
  LukeHttpConn *c = loop->done_head;
  loop->done_head = NULL;
  pthread_mutex_unlock(&loop->done_mu);
  while (c) {
    LukeHttpConn *next = c->done_next;
    c->done_next = NULL;
    if (loop->in_flight > 0) loop->in_flight--;
    if (c->state == LUKE_HTTP_ST_STREAM || c->fd < 0) {
      luke_http__ev_mod(loop, c, 0);
      luke_http__unreg_fd(loop, c);
      luke_http__live_unlink(loop, c);
      if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
      }
      luke_http__conn_free(c);
    } else if (c->out_len > c->out_off) {
      c->state = LUKE_HTTP_ST_WRITE;
      c->last_ms = luke_http__now_ms();
      luke_http__ev_mod(loop, c, 2);
    } else {
      luke_http__after_write(loop, c);
    }
    c = next;
  }
}

static inline void luke_http__apply_handle_result(LukeHttpConn *c, LukeHttpRequest *req) {
  if (c->state == LUKE_HTTP_ST_STREAM) {
    if (req->client_fd >= 0) {
      close(req->client_fd);
      req->client_fd = -1;
    }
    c->fd = -1;
  } else {
    if (req->client_fd < 0) {
      c->fd = -1;
      c->close_after_write = 1;
    }
    c->keep_alive = req->keep_alive && req->replied && req->client_fd >= 0 && !c->close_after_write;
    if (!req->replied) c->close_after_write = 1;
  }
}

static inline void luke_http__complete_conn(LukeHttpLoop *loop, LukeHttpConn *c, int from_worker) {
  if (from_worker) {
    luke_http__done_push(loop, c);
    return;
  }
  if (c->state == LUKE_HTTP_ST_STREAM || c->fd < 0) {
    luke_http__ev_mod(loop, c, 0);
    luke_http__unreg_fd(loop, c);
    luke_http__live_unlink(loop, c);
    if (c->fd >= 0) {
      close(c->fd);
      c->fd = -1;
    }
    luke_http__conn_free(c);
  } else if (c->out_len > c->out_off) {
    c->state = LUKE_HTTP_ST_WRITE;
    c->last_ms = luke_http__now_ms();
    luke_http__ev_mod(loop, c, 2);
  } else {
    luke_http__after_write(loop, c);
  }
}

static inline void luke_http__run_ev_job(LukeHttpPool *pool, LukeHttpServeJob *job, int from_worker) {
  LukeHttpConn *c = job->conn;
  LukeHttpHandler handler = job->handler ? job->handler : (pool ? pool->handler : NULL);
  LukeHttpLoop *loop = c ? c->loop : (pool ? pool->loop : NULL);
  job->conn = NULL;
  job->client_fd = -1;
  job->handler = NULL;
  if (!c || !loop) return;
  if (!handler) handler = loop->handler;

  LukeArena *arena = luke_http__arena_acquire();
  if (!arena) {
    if (c->fd >= 0) {
      close(c->fd);
      c->fd = -1;
    }
    c->state = LUKE_HTTP_ST_STREAM;
    luke_http__complete_conn(loop, c, from_worker);
    return;
  }
  size_t used = 0;
  LukeHttpRequest *req = luke_http_parse_complete(arena, c->in, c->in_len, c->fd, &used);
  if (!req) {
    luke_http__arena_release(arena);
    if (c->fd >= 0) {
      close(c->fd);
      c->fd = -1;
    }
    c->state = LUKE_HTTP_ST_STREAM;
    luke_http__complete_conn(loop, c, from_worker);
    return;
  }
  if (used < c->in_len) memmove(c->in, c->in + used, c->in_len - used);
  c->in_len -= used;

  luke_http__cur_conn = c;
  if (handler) handler(arena, req);
  luke_http__cur_conn = NULL;

  luke_http__apply_handle_result(c, req);
  luke_http__complete_conn(loop, c, from_worker);
  luke_http__arena_release(arena);
}

static inline LukeHttpServeJob *luke_http__job_acquire(LukeHttpPool *pool) {
  LukeHttpServeJob *job = NULL;
  if (pool) {
    pthread_mutex_lock(&pool->mu);
    job = pool->job_free;
    if (job) pool->job_free = job->free_next;
    pthread_mutex_unlock(&pool->mu);
  }
  if (!job) job = (LukeHttpServeJob *)calloc(1, sizeof(LukeHttpServeJob));
  else {
    job->free_next = NULL;
    job->handler = NULL;
    job->conn = NULL;
    job->client_fd = -1;
  }
  return job;
}

static inline void luke_http__job_release(LukeHttpPool *pool, LukeHttpServeJob *job) {
  if (!job) return;
  if (!pool) {
    free(job);
    return;
  }
  pthread_mutex_lock(&pool->mu);
  job->free_next = pool->job_free;
  pool->job_free = job;
  pthread_mutex_unlock(&pool->mu);
}

static inline void luke_http__run_pool_job(LukeHttpPool *pool, LukeHttpServeJob *job) {
  int cfd = job->client_fd;
  LukeHttpHandler handler = job->handler ? job->handler : pool->handler;
  luke_http__job_release(pool, job);
  int ka_left = luke_http__ka_budget();
  int unlimited_ka = ka_left == INT_MAX;
  while (cfd >= 0 && (unlimited_ka || ka_left-- > 0)) {
    LukeArena *arena = luke_http__arena_acquire();
    if (!arena) break;
    LukeHttpRequest *req = luke_http_read_request(arena, cfd);
    if (!req) {
      luke_http__arena_release(arena);
      break;
    }
    if (handler) handler(arena, req);
    int reuse = req->client_fd >= 0 && req->keep_alive && !req->streaming && req->replied;
    if (!reuse && req->client_fd >= 0) {
      close(req->client_fd);
      req->client_fd = -1;
      cfd = -1;
    } else if (reuse) {
      cfd = req->client_fd;
    } else {
      cfd = -1;
    }
    luke_http__arena_release(arena);
    if (!reuse) break;
  }
  if (cfd >= 0) close(cfd);
}

static inline int luke_http__try_enqueue(LukeHttpLoop *loop, LukeHttpConn *c) {
  if (c->ka_left != INT_MAX) {
    if (c->ka_left <= 0) {
      luke_http__conn_close(loop, c);
      return 0;
    }
    c->ka_left--;
  }
  c->pending_handle = 0;
  c->state = LUKE_HTTP_ST_HANDLE;
  luke_http__ev_mod(loop, c, 0);

  c->job.handler = loop->handler;
  c->job.client_fd = -1;
  c->job.conn = c;
  c->job.free_next = NULL;

  if (loop->inline_handlers || !loop->pool || loop->pool->nworkers <= 0) {
    luke_http__run_ev_job(loop->pool, &c->job, 0);
    return 1;
  }

  LukeHttpPool *pool = loop->pool;
  pthread_mutex_lock(&pool->mu);
  if (pool->len >= LUKE_HTTP_POOL_QUEUE || pool->stop) {
    pthread_mutex_unlock(&pool->mu);
    c->pending_handle = 1;
    c->state = LUKE_HTTP_ST_READ;
    if (c->ka_left != INT_MAX) c->ka_left++; /* undo budget burn */
    luke_http__ev_mod(loop, c, 1);
    return 0;
  }
  int slot = (pool->head + pool->len) % LUKE_HTTP_POOL_QUEUE;
  pool->q[slot] = &c->job;
  pool->len++;
  pthread_cond_signal(&pool->not_empty);
  pthread_mutex_unlock(&pool->mu);
  loop->in_flight++;
  return 1;
}

static inline void luke_http__retry_pending(LukeHttpLoop *loop) {
  LukeHttpConn *c = loop->live;
  while (c) {
    LukeHttpConn *next = c->live_next;
    if (c->pending_handle) (void)luke_http__try_enqueue(loop, c);
    c = next;
  }
}

static inline void luke_http__idle_sweep(LukeHttpLoop *loop) {
  int to = luke_http__timeout_ms();
  if (to <= 0) return;
  uint64_t now = luke_http__now_ms();
  LukeHttpConn *c = loop->live;
  while (c) {
    LukeHttpConn *next = c->live_next;
    if ((c->state == LUKE_HTTP_ST_READ || c->state == LUKE_HTTP_ST_WRITE) &&
        now - c->last_ms > (uint64_t)to)
      luke_http__conn_close(loop, c);
    c = next;
  }
}

static inline void *luke_http__pool_worker(void *arg) {
  LukeHttpPool *pool = (LukeHttpPool *)arg;
  for (;;) {
    pthread_mutex_lock(&pool->mu);
    while (pool->len == 0 && !pool->stop) pthread_cond_wait(&pool->not_empty, &pool->mu);
    if (pool->len == 0 && pool->stop) {
      pthread_mutex_unlock(&pool->mu);
      break;
    }
    LukeHttpServeJob *job = pool->q[pool->head];
    pool->head = (pool->head + 1) % LUKE_HTTP_POOL_QUEUE;
    pool->len--;
    pthread_cond_signal(&pool->not_full);
    pthread_mutex_unlock(&pool->mu);
    if (!job) continue;
    if (job->conn)
      luke_http__run_ev_job(pool, job, 1);
    else
      luke_http__run_pool_job(pool, job);
  }
  return NULL;
}

static inline int luke_http__pool_start(LukeHttpPool *pool, LukeHttpHandler handler,
                                        pthread_t *workers, int nworkers) {
  memset(pool, 0, sizeof(*pool));
  pool->handler = handler;
  pool->nworkers = nworkers < 0 ? 0 : nworkers;
  pthread_mutex_init(&pool->mu, NULL);
  pthread_cond_init(&pool->not_empty, NULL);
  pthread_cond_init(&pool->not_full, NULL);
  for (int i = 0; i < pool->nworkers; ++i) {
    if (pthread_create(&workers[i], NULL, luke_http__pool_worker, pool) != 0) {
      pool->stop = 1;
      pthread_cond_broadcast(&pool->not_empty);
      for (int j = 0; j < i; ++j) pthread_join(workers[j], NULL);
      pthread_mutex_destroy(&pool->mu);
      pthread_cond_destroy(&pool->not_empty);
      pthread_cond_destroy(&pool->not_full);
      return 0;
    }
  }
  return 1;
}

static inline void luke_http__pool_join(LukeHttpPool *pool, pthread_t *workers) {
  pthread_mutex_lock(&pool->mu);
  pool->stop = 1;
  pthread_cond_broadcast(&pool->not_empty);
  pthread_cond_broadcast(&pool->not_full);
  pthread_mutex_unlock(&pool->mu);
  for (int i = 0; i < pool->nworkers; ++i) pthread_join(workers[i], NULL);
  while (pool->job_free) {
    LukeHttpServeJob *j = pool->job_free;
    pool->job_free = j->free_next;
    free(j);
  }
  pthread_mutex_destroy(&pool->mu);
  pthread_cond_destroy(&pool->not_empty);
  pthread_cond_destroy(&pool->not_full);
}

static inline void luke_http__install_signals(void) {
  luke_http__stop_flag = 0;
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = luke_http__on_signal;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
}

/* Legacy path: accept enqueues bare fds; workers block on recv/send. */
static inline int luke_http_serve_pool(LukeHttpServer *s, LukeHttpHandler handler, double max_conn) {
  if (!s || s->fd < 0 || !handler) return 0;
  int left = (int)max_conn;
  int unlimited = left <= 0;
  int started = 0;
  int nworkers = luke_http__workers_per_loop(1);
  if (nworkers < 1) nworkers = LUKE_HTTP_POOL_WORKERS > 0 ? LUKE_HTTP_POOL_WORKERS : 8;

  {
    int fl = fcntl(s->fd, F_GETFL, 0);
    if (fl >= 0) fcntl(s->fd, F_SETFL, fl | O_NONBLOCK);
  }

  LukeHttpPool pool;
  pthread_t workers[64];
  if (nworkers > 64) nworkers = 64;
  if (!luke_http__pool_start(&pool, handler, workers, nworkers)) return 0;

  while ((unlimited || left > 0) && !luke_http__stop_flag) {
    struct pollfd pfd;
    pfd.fd = s->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int pr = poll(&pfd, 1, 250);
    if (luke_http__stop_flag) break;
    if (pr < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (pr == 0) continue;
    if (!(pfd.revents & POLLIN)) continue;

    for (;;) {
#if defined(__linux__) && defined(SOCK_NONBLOCK)
      int cfd = accept4(s->fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
      int cfd = accept(s->fd, NULL, NULL);
#endif
      if (cfd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        luke_http__stop_flag = 1;
        break;
      }
#if !(defined(__linux__) && defined(SOCK_NONBLOCK))
      if (luke_http__set_nb(cfd) < 0) {
        close(cfd);
        continue;
      }
#endif
      luke_http__set_nodelay(cfd);
      luke_http__set_timeouts(cfd);

      LukeHttpServeJob *job = luke_http__job_acquire(&pool);
      if (!job) {
        close(cfd);
        break;
      }
      job->handler = handler;
      job->client_fd = cfd;
      job->conn = NULL;

      pthread_mutex_lock(&pool.mu);
      while (pool.len >= LUKE_HTTP_POOL_QUEUE && !pool.stop && !luke_http__stop_flag)
        pthread_cond_wait(&pool.not_full, &pool.mu);
      if (pool.stop || luke_http__stop_flag) {
        pthread_mutex_unlock(&pool.mu);
        close(cfd);
        luke_http__job_release(&pool, job);
        break;
      }
      int slot = (pool.head + pool.len) % LUKE_HTTP_POOL_QUEUE;
      pool.q[slot] = job;
      pool.len++;
      pthread_cond_signal(&pool.not_empty);
      pthread_mutex_unlock(&pool.mu);

      started++;
      if (!unlimited) {
        left--;
        if (left <= 0) break;
      }
    }
  }

  luke_http__pool_join(&pool, workers);
  return started > 0 ? 1 : 0;
}

static inline int luke_http__accept_client(int listen_fd) {
#if defined(__linux__) && defined(SOCK_NONBLOCK)
  int cfd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
  int cfd = accept(listen_fd, NULL, NULL);
#endif
  if (cfd < 0) return -1;
#if !(defined(__linux__) && defined(SOCK_NONBLOCK))
  if (luke_http__set_nb(cfd) < 0) {
    close(cfd);
    errno = EAGAIN;
    return -1;
  }
#endif
  luke_http__set_nodelay(cfd);
  return cfd;
}

static inline int luke_http__budget_take(LukeHttpLoop *loop) {
  if (!loop->budget_mu) return 1;
  pthread_mutex_lock(loop->budget_mu);
  if (!loop->unlimited) {
    if (!loop->budget_left || *loop->budget_left <= 0) {
      pthread_mutex_unlock(loop->budget_mu);
      return 0;
    }
    (*loop->budget_left)--;
  }
  if (loop->budget_started) (*loop->budget_started)++;
  int left = loop->unlimited ? 1 : (loop->budget_left ? *loop->budget_left : 0);
  pthread_mutex_unlock(loop->budget_mu);
  if (!loop->unlimited && left <= 0) {
    loop->listen_on = 0;
    luke_http__ev_del_fd(loop, loop->listen_fd);
  }
  return 1;
}

static inline int luke_http__accept_one(LukeHttpLoop *loop, int *left, int unlimited, int *started) {
  int cfd = luke_http__accept_client(loop->listen_fd);
  if (cfd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -1;
  }
  if (loop->budget_mu) {
    if (!luke_http__budget_take(loop)) {
      close(cfd);
      return 0;
    }
  } else {
    if (!unlimited) {
      if (*left <= 0) {
        close(cfd);
        loop->listen_on = 0;
        luke_http__ev_del_fd(loop, loop->listen_fd);
        return 0;
      }
      (*left)--;
      if (*left <= 0) {
        loop->listen_on = 0;
        luke_http__ev_del_fd(loop, loop->listen_fd);
      }
    }
    if (started) (*started)++;
  }

  LukeHttpConn *c = (LukeHttpConn *)calloc(1, sizeof(LukeHttpConn));
  if (!c) {
    close(cfd);
    return 0;
  }
  c->fd = cfd;
  c->state = LUKE_HTTP_ST_READ;
  c->ka_left = luke_http__ka_budget();
  c->loop = loop;
  c->last_ms = luke_http__now_ms();
  luke_http__live_link(loop, c);
  luke_http__reg_fd(loop, c);
  luke_http__ev_mod(loop, c, 1);
  return 1;
}

static inline int luke_http__listen_on_port(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int yes = 1;
  (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) < 0) {
    close(fd);
    return -1;
  }
#else
  close(fd);
  return -1;
#endif
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)port);
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, LUKE_HTTP_BACKLOG) < 0) {
    close(fd);
    return -1;
  }
  if (luke_http__set_nb(fd) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static inline int luke_http__port_of(int fd) {
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);
  if (getsockname(fd, (struct sockaddr *)&addr, &len) != 0) return -1;
  return (int)ntohs(addr.sin_port);
}

typedef struct LukeHttpLoopThreadArg {
  int listen_fd;
  int own_listen;
  LukeHttpHandler handler;
  int nworkers;
  int inline_handlers;
  pthread_mutex_t *budget_mu;
  int *budget_left;
  int *budget_started;
  int unlimited;
  int *out_started; /* local fallback when no shared budget */
} LukeHttpLoopThreadArg;

/*
 * One event-loop thread: owns accept/read/write for its listen fd.
 * Optional per-loop handler pool; LUKE_HTTP_INLINE=1 runs handlers on this thread.
 */
static inline int luke_http_serve_ev_one(LukeHttpLoopThreadArg *arg) {
  if (!arg || arg->listen_fd < 0 || !arg->handler) return 0;
  int left = 0;
  int unlimited = arg->unlimited;
  int started = 0;
  int *started_ptr = arg->budget_started ? arg->budget_started : &started;

  LukeHttpLoop loop;
  memset(&loop, 0, sizeof(loop));
  loop.listen_fd = arg->listen_fd;
  loop.listen_on = 1;
  loop.own_listen = arg->own_listen;
  loop.handler = arg->handler;
  loop.inline_handlers = arg->inline_handlers || arg->nworkers <= 0;
  loop.wake[0] = loop.wake[1] = -1;
  loop.budget_mu = arg->budget_mu;
  loop.budget_left = arg->budget_left;
  loop.budget_started = arg->budget_started;
  loop.unlimited = unlimited;
  pthread_mutex_init(&loop.done_mu, NULL);
  luke_http__ev_open(&loop);
  if (pipe(loop.wake) != 0) {
    if (loop.evfd >= 0) close(loop.evfd);
    pthread_mutex_destroy(&loop.done_mu);
    return 0;
  }
  (void)luke_http__set_nb(loop.wake[0]);
  (void)luke_http__set_nb(loop.wake[1]);
  if (loop.kind != 0) {
    if (luke_http__ev_add_special(&loop, loop.listen_fd, LUKE_HTTP_EV_LISTEN) < 0 ||
        luke_http__ev_add_special(&loop, loop.wake[0], LUKE_HTTP_EV_WAKE) < 0) {
      close(loop.wake[0]);
      close(loop.wake[1]);
      if (loop.evfd >= 0) close(loop.evfd);
      pthread_mutex_destroy(&loop.done_mu);
      return 0;
    }
  }

  LukeHttpPool pool;
  pthread_t workers[64];
  int nworkers = arg->nworkers;
  if (nworkers > 64) nworkers = 64;
  if (loop.inline_handlers) nworkers = 0;
  if (!luke_http__pool_start(&pool, arg->handler, workers, nworkers)) {
    close(loop.wake[0]);
    close(loop.wake[1]);
    if (loop.evfd >= 0) close(loop.evfd);
    pthread_mutex_destroy(&loop.done_mu);
    return 0;
  }
  pool.loop = &loop;
  loop.pool = &pool;

  while (!luke_http__stop_flag) {
    if (!unlimited) {
      int rem = 1;
      if (loop.budget_mu && loop.budget_left) {
        pthread_mutex_lock(loop.budget_mu);
        rem = *loop.budget_left;
        pthread_mutex_unlock(loop.budget_mu);
      } else {
        rem = left;
      }
      if (rem <= 0 && loop.live_n == 0 && loop.in_flight == 0) break;
    }
    luke_http__drain_done(&loop);
    luke_http__retry_pending(&loop);
    luke_http__idle_sweep(&loop);

    LukeHttpEvHit hits[64];
    int n = luke_http__ev_wait(&loop, hits, 64, 250);
    if (n < 0) break;
    for (int i = 0; i < n; ++i) {
      void *who = hits[i].who;
      int ev = hits[i].ev;
      if (who == LUKE_HTTP_EV_LISTEN) {
        if (!loop.listen_on) continue;
        for (;;) {
          int ar = luke_http__accept_one(&loop, &left, unlimited, &started);
          if (ar <= 0) {
            if (ar < 0) luke_http__stop_flag = 1;
            break;
          }
        }
        continue;
      }
      if (who == LUKE_HTTP_EV_WAKE) {
        luke_http__wake_eat(&loop);
        luke_http__drain_done(&loop);
        continue;
      }
      LukeHttpConn *c = (LukeHttpConn *)who;
      if (!c || c->fd < 0) continue;
      if (c->state == LUKE_HTTP_ST_HANDLE || c->state == LUKE_HTTP_ST_STREAM) continue;
      if ((ev & 4) && c->state == LUKE_HTTP_ST_READ && !(ev & 1)) {
        luke_http__conn_close(&loop, c);
        continue;
      }
      if ((ev & 1) && c->state == LUKE_HTTP_ST_READ) {
        c->last_ms = luke_http__now_ms();
        int rr = luke_http__nb_read(c);
        if (rr < 0) {
          luke_http__conn_close(&loop, c);
          continue;
        }
        if (rr > 0) {
          (void)luke_http__try_enqueue(&loop, c);
          continue;
        }
      }
      if ((ev & 2) && c->state == LUKE_HTTP_ST_WRITE) {
        c->last_ms = luke_http__now_ms();
        int wr = luke_http__nb_flush(c);
        if (wr < 0)
          luke_http__conn_close(&loop, c);
        else if (wr > 0)
          luke_http__after_write(&loop, c);
      }
    }
  }

  {
    LukeHttpConn *c = loop.live;
    while (c) {
      LukeHttpConn *next = c->live_next;
      if (c->state == LUKE_HTTP_ST_READ || c->state == LUKE_HTTP_ST_WRITE)
        luke_http__conn_close(&loop, c);
      c = next;
    }
  }
  luke_http__pool_join(&pool, workers);
  luke_http__drain_done(&loop);
  while (loop.live) luke_http__conn_close(&loop, loop.live);

  close(loop.wake[0]);
  close(loop.wake[1]);
  if (loop.evfd >= 0) close(loop.evfd);
  if (loop.own_listen && loop.listen_fd >= 0) close(loop.listen_fd);
  free(loop.by_fd);
  free(loop.pfds);
  pthread_mutex_destroy(&loop.done_mu);
  if (arg->out_started) *arg->out_started = started;
  (void)started_ptr;
  return (arg->budget_started ? *arg->budget_started : started) > 0 ? 1 : 0;
}

static inline void *luke_http__loop_thread(void *arg) {
  LukeHttpLoopThreadArg *a = (LukeHttpLoopThreadArg *)arg;
  (void)luke_http_serve_ev_one(a);
  return NULL;
}

/*
 * Event-loop I/O (epoll / kqueue / poll) owns every socket.
 * Default: one loop per CPU with SO_REUSEPORT; each loop has its own epoll and
 * a small handler pool (or run-to-completion when LUKE_HTTP_INLINE=1).
 * Workers never block on recv. SSE/chunked leave the loop.
 */
static inline int luke_http_serve_ev(LukeHttpServer *s, LukeHttpHandler handler, double max_conn) {
  if (!s || s->fd < 0 || !handler) return 0;
  int left = (int)max_conn;
  int unlimited = left <= 0;
  int nloops = luke_http__nloops();
  int nworkers = luke_http__workers_per_loop(nloops);
  int inline_handlers = luke_http__want_inline() || nworkers <= 0;
  int port = luke_http__port_of(s->fd);
  if (port < 0) return 0;

  if (luke_http__set_nb(s->fd) < 0) return 0;

#ifdef SO_REUSEPORT
  if (nloops > 1) {
    /* Replace the caller's listen fd with N REUSEPORT listeners. */
    int fds[64];
    if (nloops > 64) nloops = 64;
    close(s->fd);
    s->fd = -1;
    for (int i = 0; i < nloops; ++i) {
      fds[i] = luke_http__listen_on_port(port);
      if (fds[i] < 0) {
        for (int j = 0; j < i; ++j) close(fds[j]);
        /* Fall back to single loop — re-listen. */
        nloops = 1;
        s->fd = luke_http__listen_on_port(port);
        if (s->fd < 0) return 0;
        break;
      }
    }
    if (nloops > 1) {
      pthread_mutex_t budget_mu;
      int budget_left = left;
      int budget_started = 0;
      pthread_mutex_init(&budget_mu, NULL);
      pthread_t threads[64];
      LukeHttpLoopThreadArg args[64];
      memset(args, 0, sizeof(args));
      for (int i = 0; i < nloops; ++i) {
        args[i].listen_fd = fds[i];
        args[i].own_listen = 1;
        args[i].handler = handler;
        args[i].nworkers = nworkers;
        args[i].inline_handlers = inline_handlers;
        args[i].budget_mu = &budget_mu;
        args[i].budget_left = unlimited ? NULL : &budget_left;
        args[i].budget_started = &budget_started;
        args[i].unlimited = unlimited;
        if (pthread_create(&threads[i], NULL, luke_http__loop_thread, &args[i]) != 0) {
          luke_http__stop_flag = 1;
          for (int j = 0; j < i; ++j) pthread_join(threads[j], NULL);
          for (int j = i; j < nloops; ++j) close(fds[j]);
          pthread_mutex_destroy(&budget_mu);
          return budget_started > 0 ? 1 : 0;
        }
      }
      for (int i = 0; i < nloops; ++i) pthread_join(threads[i], NULL);
      pthread_mutex_destroy(&budget_mu);
      return budget_started > 0 ? 1 : 0;
    }
  }
#else
  (void)nloops;
#endif

  {
    pthread_mutex_t budget_mu;
    int budget_left = left;
    int budget_started = 0;
    pthread_mutex_init(&budget_mu, NULL);
    LukeHttpLoopThreadArg arg;
    memset(&arg, 0, sizeof(arg));
    arg.listen_fd = s->fd;
    arg.own_listen = 0;
    arg.handler = handler;
    arg.nworkers = nworkers;
    arg.inline_handlers = inline_handlers;
    arg.budget_mu = &budget_mu;
    arg.budget_left = unlimited ? NULL : &budget_left;
    arg.budget_started = &budget_started;
    arg.unlimited = unlimited;
    (void)luke_http_serve_ev_one(&arg);
    pthread_mutex_destroy(&budget_mu);
    return budget_started > 0 ? 1 : 0;
  }
}

/* Accept up to max_conn connections (max_conn<=0 → forever until stop/accept fails).
 * Default: N SO_REUSEPORT event loops (one per CPU) + per-loop handler pools.
 * LUKE_HTTP_INLINE=1 → run-to-completion on the loop thread (REST bench).
 * LUKE_HTTP_IO=pool → legacy blocking worker recv/send.
 * Keep-alive: LUKE_HTTP_KEEPALIVE_MAX (default 100000; 0 = unlimited). */
static inline int luke_http_serve(LukeHttpServer *s, LukeHttpHandler handler, double max_conn) {
  if (!s || s->fd < 0 || !handler) return 0;
  luke_http__install_signals();
  if (luke_http__want_pool_io()) return luke_http_serve_pool(s, handler, max_conn);
  return luke_http_serve_ev(s, handler, max_conn);
}

#endif /* !__wasi__ */

static inline LukeText luke_http_path(LukeHttpRequest *req) {
  return req ? req->path : luke_text("");
}

static inline LukeText luke_http_method(LukeHttpRequest *req) {
  return req ? req->method : luke_text("");
}

static inline LukeText luke_http_query(LukeHttpRequest *req) {
  return req ? req->query : luke_text("");
}

static inline LukeText luke_http_body(LukeHttpRequest *req) {
  return req ? req->body : luke_text("");
}

static inline LukeText luke_http_last_event_id(LukeHttpRequest *req) {
  return req ? req->last_event_id : luke_text("");
}

/* ---------- Request parsing / routing beachhead ---------- */

static inline int luke_http__hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static inline LukeText luke_http__url_decode(LukeArena *a, const char *s, size_t n) {
  char *out = (char *)luke_arena_alloc(a, n + 1, 1);
  size_t o = 0;
  for (size_t i = 0; i < n; ++i) {
    if (s[i] == '+' ) {
      out[o++] = ' ';
    } else if (s[i] == '%' && i + 2 < n) {
      int hi = luke_http__hex(s[i + 1]);
      int lo = luke_http__hex(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out[o++] = (char)((hi << 4) | lo);
        i += 2;
      } else {
        out[o++] = s[i];
      }
    } else {
      out[o++] = s[i];
    }
  }
  out[o] = '\0';
  return luke_text_n(out, o);
}

static inline LukeText luke_http__header_raw(LukeArena *a, LukeHttpRequest *req, LukeText name) {
  if (!req || !name.len || !req->headers.ptr) return luke_text("");
  const char *buf = req->headers.ptr;
  size_t hdr_end = req->headers.len;
  size_t h = 0;
  while (h < hdr_end) {
    size_t line0 = h;
    while (h < hdr_end && buf[h] != '\n') ++h;
    size_t line1 = h;
    if (h < hdr_end) ++h;
    size_t L = line1 > line0 && buf[line1 - 1] == '\r' ? line1 - 1 : line1;
    if (L <= line0) continue;
    if (L - line0 < name.len + 1) continue;
    int match = 1;
    for (size_t k = 0; k < name.len; ++k) {
      char c = buf[line0 + k];
      char e = name.ptr[k];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      if (e >= 'A' && e <= 'Z') e = (char)(e - 'A' + 'a');
      if (c != e) {
        match = 0;
        break;
      }
    }
    if (!match) continue;
    if (buf[line0 + name.len] != ':') continue;
    size_t v0 = line0 + name.len + 1;
    while (v0 < L && (buf[v0] == ' ' || buf[v0] == '\t')) ++v0;
    return luke_http__dup(a, buf + v0, L - v0);
  }
  return luke_text("");
}

static inline LukeText luke_http_header(LukeArena *a, LukeHttpRequest *req, LukeText name) {
  return luke_http__header_raw(a, req, name);
}

static inline LukeText luke_http_cookie(LukeArena *a, LukeHttpRequest *req, LukeText name) {
  LukeText cookie = luke_http__header_raw(a, req, luke_text("cookie"));
  if (!cookie.len || !name.len) return luke_text("");
  size_t i = 0;
  while (i < cookie.len) {
    while (i < cookie.len && (cookie.ptr[i] == ' ' || cookie.ptr[i] == ';')) ++i;
    size_t k0 = i;
    while (i < cookie.len && cookie.ptr[i] != '=' && cookie.ptr[i] != ';') ++i;
    size_t k1 = i;
    while (k1 > k0 && cookie.ptr[k1 - 1] == ' ') --k1;
    if (i < cookie.len && cookie.ptr[i] == '=') ++i;
    size_t v0 = i;
    while (i < cookie.len && cookie.ptr[i] != ';') ++i;
    size_t v1 = i;
    if (k1 - k0 == name.len && memcmp(cookie.ptr + k0, name.ptr, name.len) == 0)
      return luke_http__dup(a, cookie.ptr + v0, v1 - v0);
  }
  return luke_text("");
}

#ifndef LUKE_COOKIE_HTTPONLY
#define LUKE_COOKIE_HTTPONLY 1
#define LUKE_COOKIE_SECURE 2
#define LUKE_COOKIE_SAMESITE_LAX 4
#define LUKE_COOKIE_SAMESITE_STRICT 8
#define LUKE_COOKIE_CLEAR 16
#endif

static inline int luke_http_set_cookie_ex(LukeArena *a, LukeHttpRequest *req, LukeText name,
                                          LukeText value, int flags) {
  if (!req || !a || !name.len) return 0;
  /* name=value; Path=/; [HttpOnly]; [Secure]; [SameSite=…]; [Max-Age=0] */
  size_t n = name.len + 1 + value.len + 96;
  char *line = (char *)luke_arena_alloc(a, n + 1, 1);
  size_t o = 0;
  memcpy(line + o, name.ptr, name.len);
  o += name.len;
  line[o++] = '=';
  if (!(flags & LUKE_COOKIE_CLEAR) && value.len && value.ptr) {
    memcpy(line + o, value.ptr, value.len);
    o += value.len;
  }
  memcpy(line + o, "; Path=/", 8);
  o += 8;
  if (flags & LUKE_COOKIE_CLEAR) {
    memcpy(line + o, "; Max-Age=0", 11);
    o += 11;
  }
  if (flags & LUKE_COOKIE_HTTPONLY) {
    memcpy(line + o, "; HttpOnly", 10);
    o += 10;
  }
  if (flags & LUKE_COOKIE_SECURE) {
    memcpy(line + o, "; Secure", 8);
    o += 8;
  }
  if (flags & LUKE_COOKIE_SAMESITE_STRICT) {
    memcpy(line + o, "; SameSite=Strict", 17);
    o += 17;
  } else if (flags & LUKE_COOKIE_SAMESITE_LAX) {
    memcpy(line + o, "; SameSite=Lax", 14);
    o += 14;
  }
  line[o] = '\0';

  /* Append to pending cookie block (newline-separated). */
  if (req->set_cookie.len == 0) {
    req->set_cookie = luke_text_n(line, o);
    return 1;
  }
  size_t total = req->set_cookie.len + 1 + o;
  char *block = (char *)luke_arena_alloc(a, total + 1, 1);
  memcpy(block, req->set_cookie.ptr, req->set_cookie.len);
  block[req->set_cookie.len] = '\n';
  memcpy(block + req->set_cookie.len + 1, line, o);
  block[total] = '\0';
  req->set_cookie = luke_text_n(block, total);
  return 1;
}

static inline int luke_http_set_cookie(LukeArena *a, LukeHttpRequest *req, LukeText name,
                                       LukeText value) {
  /* Legacy helper — HttpOnly + SameSite=Lax (not Secure; set LUKE_AUTH_SECURE for that). */
  return luke_http_set_cookie_ex(a, req, name, value,
                                 LUKE_COOKIE_HTTPONLY | LUKE_COOKIE_SAMESITE_LAX);
}

static inline LukeMap *luke_http_query_map(LukeArena *a, LukeHttpRequest *req) {
  LukeMap *m = luke_map_new(a);
  if (!req || !req->query.len || !req->query.ptr) return m;
  const char *q = req->query.ptr;
  size_t n = req->query.len;
  size_t i = 0;
  while (i < n) {
    size_t k0 = i;
    while (i < n && q[i] != '=' && q[i] != '&') ++i;
    size_t k1 = i;
    size_t v0 = i, v1 = i;
    if (i < n && q[i] == '=') {
      ++i;
      v0 = i;
      while (i < n && q[i] != '&') ++i;
      v1 = i;
    }
    if (i < n && q[i] == '&') ++i;
    if (k1 > k0) {
      LukeText key = luke_http__url_decode(a, q + k0, k1 - k0);
      LukeText val = luke_http__url_decode(a, q + v0, v1 - v0);
      luke_map_put(a, m, key, val);
    }
  }
  return m;
}

/* application/x-www-form-urlencoded body → MAP (same decoder as query). */
static inline LukeMap *luke_http_form_map(LukeArena *a, LukeHttpRequest *req) {
  LukeMap *m = luke_map_new(a);
  if (!req || !req->body.len || !req->body.ptr) return m;
  const char *q = req->body.ptr;
  size_t n = req->body.len;
  size_t i = 0;
  while (i < n) {
    size_t k0 = i;
    while (i < n && q[i] != '=' && q[i] != '&') ++i;
    size_t k1 = i;
    size_t v0 = i, v1 = i;
    if (i < n && q[i] == '=') {
      ++i;
      v0 = i;
      while (i < n && q[i] != '&') ++i;
      v1 = i;
    }
    if (i < n && q[i] == '&') ++i;
    if (k1 > k0) {
      LukeText key = luke_http__url_decode(a, q + k0, k1 - k0);
      LukeText val = luke_http__url_decode(a, q + v0, v1 - v0);
      luke_map_put(a, m, key, val);
    }
  }
  return m;
}

/* Match path against pattern with :param segments. Fills out MAP; returns 1 on match. */
static inline int luke_http_match(LukeArena *a, LukeText path, LukeText pattern, LukeMap *out) {
  if (!a || !out) return 0;
  /* Clear prior entries by replacing with empty map contents is caller's job —
   * we only put captures; caller should use a fresh MAP. */
  const char *p = path.ptr ? path.ptr : "";
  const char *pat = pattern.ptr ? pattern.ptr : "";
  size_t plen = path.len, patlen = pattern.len;
  size_t i = 0, j = 0;
  /* Allow optional leading slash normalization already present in both. */
  while (i <= plen && j <= patlen) {
    /* End of both → success */
    if (i == plen && j == patlen) return 1;
    /* Advance over '/' */
    if (i < plen && p[i] == '/') ++i;
    if (j < patlen && pat[j] == '/') ++j;
    if (i == plen && j == patlen) return 1;
    if (i == plen || j == patlen) return 0;

    size_t ps = i;
    while (i < plen && p[i] != '/') ++i;
    size_t pe = i;
    size_t qs = j;
    while (j < patlen && pat[j] != '/') ++j;
    size_t qe = j;

    if (qe > qs && pat[qs] == ':') {
      /* capture */
      LukeText key = luke_http__dup(a, pat + qs + 1, qe - qs - 1);
      LukeText val = luke_http__url_decode(a, p + ps, pe - ps);
      luke_map_put(a, out, key, val);
    } else {
      if (pe - ps != qe - qs) return 0;
      if (pe > ps && memcmp(p + ps, pat + qs, pe - ps) != 0) return 0;
    }
  }
  return i == plen && j == patlen;
}

#ifdef __cplusplus
}
#endif

#endif /* LUKE_NET_H */
