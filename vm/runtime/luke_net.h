#ifndef LUKE_NET_H
#define LUKE_NET_H

/* Minimal HTTP/1.1 server for Luke Build mode (native only; stubbed on WASI). */

#include "luke_rt.h"

#if !defined(__wasi__)
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
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

static inline LukeHttpServer *luke_http_listen(LukeArena *a, double port) {
  int p = (int)port;
  if (p <= 0 || p > 65535) return NULL;

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return NULL;

  int yes = 1;
  (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)p);

  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return NULL;
  }
#ifndef LUKE_HTTP_BACKLOG
#define LUKE_HTTP_BACKLOG 512
#endif
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

#ifndef LUKE_HTTP_TIMEOUT_MS
#define LUKE_HTTP_TIMEOUT_MS 10000
#endif
#ifndef LUKE_HTTP_KEEPALIVE_MAX
#define LUKE_HTTP_KEEPALIVE_MAX 100
#endif

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

/* Parse one HTTP request from an already-accepted client fd (worker-side). */
static inline LukeHttpRequest *luke_http_read_request(LukeArena *a, int cfd) {
  if (!a || cfd < 0) return NULL;

  size_t cap = 8192, len = 0;
  char *buf = (char *)malloc(cap);
  if (!buf) return NULL;

  int hdr_end = -1;
  long need_body = -1;
  for (;;) {
    if (len + 2048 > cap) {
      cap *= 2;
      char *nb = (char *)realloc(buf, cap);
      if (!nb) {
        free(buf);
        return NULL;
      }
      buf = nb;
    }
    ssize_t got = recv(cfd, buf + len, cap - len - 1, 0);
    if (got <= 0) {
      free(buf);
      return NULL;
    }
    len += (size_t)got;
    buf[len] = '\0';

    if (hdr_end < 0) {
      hdr_end = luke_http__hdr_end(buf, len);
      if (hdr_end < 0) {
        if (len > (1u << 20)) {
          free(buf);
          return NULL;
        }
        continue;
      }
      need_body = luke_http__content_length(buf, (size_t)hdr_end);
      if (need_body < 0) need_body = 0;
      if (need_body > (1L << 20)) {
        free(buf);
        return NULL;
      }
    }

    if (hdr_end >= 0 && (long)(len - (size_t)hdr_end) >= need_body) break;
  }

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
  free(buf);

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

  size_t sent = 0;
  while (sent < (size_t)hlen) {
    ssize_t n = send(req->client_fd, hdr + sent, (size_t)hlen - sent, MSG_NOSIGNAL);
    if (n <= 0) {
      close(req->client_fd);
      req->client_fd = -1;
      return 0;
    }
    sent += (size_t)n;
  }
  sent = 0;
  while (sent < body.len) {
    ssize_t n = send(req->client_fd, body.ptr + sent, body.len - sent, MSG_NOSIGNAL);
    if (n <= 0) {
      close(req->client_fd);
      req->client_fd = -1;
      return 0;
    }
    sent += (size_t)n;
  }

  req->replied = 1;
  if (!ka) {
    close(req->client_fd);
    req->client_fd = -1;
  }
  return 1;
}

/* ---------- SSE (Server-Sent Events) — keep connection open ---------- */

static inline int luke_http__send_all(int fd, const char *buf, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
    if (n <= 0) return 0;
    sent += (size_t)n;
  }
  return 1;
}

static inline int luke_http_sse_open(LukeHttpRequest *req) {
  if (!req || req->client_fd < 0) return 0;
  req->streaming = 1;
  req->keep_alive = 0;
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

/* ---------- Concurrent serve (bounded worker pool) ---------- */

typedef void (*LukeHttpHandler)(LukeArena *arena, LukeHttpRequest *req);

typedef struct LukeHttpServeJob {
  LukeHttpHandler handler;
  int client_fd; /* parse happens on the worker — accept path stays non-blocking */
} LukeHttpServeJob;

#ifndef LUKE_HTTP_POOL_WORKERS
#define LUKE_HTTP_POOL_WORKERS 8
#endif
#ifndef LUKE_HTTP_POOL_QUEUE
#define LUKE_HTTP_POOL_QUEUE 64
#endif

typedef struct LukeHttpPool {
  pthread_mutex_t mu;
  pthread_cond_t not_empty;
  pthread_cond_t not_full;
  LukeHttpServeJob *q[LUKE_HTTP_POOL_QUEUE];
  int head;
  int len;
  int stop;
  LukeHttpHandler handler;
} LukeHttpPool;

static volatile sig_atomic_t luke_http__stop_flag = 0;

static void luke_http__on_signal(int sig) {
  (void)sig;
  luke_http__stop_flag = 1;
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

    if (job) {
      int cfd = job->client_fd;
      LukeHttpHandler handler = job->handler ? job->handler : pool->handler;
      free(job);
      int ka_left = LUKE_HTTP_KEEPALIVE_MAX;
      while (cfd >= 0 && ka_left-- > 0) {
        LukeArena *arena = (LukeArena *)calloc(1, sizeof(LukeArena));
        if (!arena) break;
        luke_arena_init(arena, 1u << 16);
        LukeHttpRequest *req = luke_http_read_request(arena, cfd);
        if (!req) {
          luke_arena_free(arena);
          free(arena);
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
        luke_arena_free(arena);
        free(arena);
        if (!reuse) break;
      }
      if (cfd >= 0) close(cfd);
    }
  }
  return NULL;
}

/* Accept up to max_conn connections (max_conn<=0 → forever until stop/accept fails).
 * Bounded worker pool: accept enqueues bare fds; workers parse + handle (slow clients
 * no longer stall the accept loop). Evented accept: O_NONBLOCK listen + poll(POLLIN).
 * SIGTERM/SIGINT → graceful stop (drain queue, join workers).
 * Per-socket SO_RCVTIMEO/SO_SNDTIMEO (LUKE_HTTP_TIMEOUT_MS, default 30s).
 * HTTP/1.1 keep-alive reuse up to LUKE_HTTP_KEEPALIVE_MAX requests per connection. */
static inline int luke_http_serve(LukeHttpServer *s, LukeHttpHandler handler, double max_conn) {
  if (!s || s->fd < 0 || !handler) return 0;
  int left = (int)max_conn;
  int unlimited = left <= 0;
  int started = 0;

  luke_http__stop_flag = 0;
  {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = luke_http__on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
  }

  {
    int fl = fcntl(s->fd, F_GETFL, 0);
    if (fl >= 0) fcntl(s->fd, F_SETFL, fl | O_NONBLOCK);
  }

  LukeHttpPool pool;
  memset(&pool, 0, sizeof(pool));
  pool.handler = handler;
  pthread_mutex_init(&pool.mu, NULL);
  pthread_cond_init(&pool.not_empty, NULL);
  pthread_cond_init(&pool.not_full, NULL);

  pthread_t workers[LUKE_HTTP_POOL_WORKERS];
  for (int i = 0; i < LUKE_HTTP_POOL_WORKERS; ++i) {
    if (pthread_create(&workers[i], NULL, luke_http__pool_worker, &pool) != 0) {
      pool.stop = 1;
      pthread_cond_broadcast(&pool.not_empty);
      for (int j = 0; j < i; ++j) pthread_join(workers[j], NULL);
      pthread_mutex_destroy(&pool.mu);
      pthread_cond_destroy(&pool.not_empty);
      pthread_cond_destroy(&pool.not_full);
      return 0;
    }
  }

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
      int cfd = accept(s->fd, NULL, NULL);
      if (cfd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        luke_http__stop_flag = 1;
        break;
      }
      luke_http__set_timeouts(cfd);

      LukeHttpServeJob *job = (LukeHttpServeJob *)calloc(1, sizeof(LukeHttpServeJob));
      if (!job) {
        close(cfd);
        break;
      }
      job->handler = handler;
      job->client_fd = cfd;

      pthread_mutex_lock(&pool.mu);
      while (pool.len >= LUKE_HTTP_POOL_QUEUE && !pool.stop && !luke_http__stop_flag)
        pthread_cond_wait(&pool.not_full, &pool.mu);
      if (pool.stop || luke_http__stop_flag) {
        pthread_mutex_unlock(&pool.mu);
        close(cfd);
        free(job);
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

  pthread_mutex_lock(&pool.mu);
  pool.stop = 1;
  pthread_cond_broadcast(&pool.not_empty);
  pthread_cond_broadcast(&pool.not_full);
  pthread_mutex_unlock(&pool.mu);
  for (int i = 0; i < LUKE_HTTP_POOL_WORKERS; ++i) pthread_join(workers[i], NULL);
  pthread_mutex_destroy(&pool.mu);
  pthread_cond_destroy(&pool.not_empty);
  pthread_cond_destroy(&pool.not_full);
  return started > 0 ? 1 : 0;
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
