#ifndef LUKE_NET_H
#define LUKE_NET_H

/* Minimal HTTP/1.1 server for Luke Build mode (native only; stubbed on WASI). */

#include "luke_rt.h"

#if !defined(__wasi__)
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
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
  int client_fd;
} LukeHttpRequest;

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

static inline int luke_http_close(LukeHttpRequest *req) {
  (void)req;
  return 0;
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
  if (listen(fd, 16) < 0) {
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

static inline LukeText luke_http__dup(LukeArena *a, const char *s, size_t n) {
  char *p = (char *)luke_arena_alloc(a, n + 1, 1);
  if (n) memcpy(p, s, n);
  p[n] = '\0';
  return luke_text_n(p, n);
}

static inline LukeHttpRequest *luke_http_accept(LukeArena *a, LukeHttpServer *s) {
  if (!s || s->fd < 0) return NULL;

  int cfd = accept(s->fd, NULL, NULL);
  if (cfd < 0) return NULL;

  size_t cap = 8192, len = 0;
  char *buf = (char *)malloc(cap);
  if (!buf) {
    close(cfd);
    return NULL;
  }

  int hdr_end = -1;
  long need_body = -1;
  for (;;) {
    if (len + 2048 > cap) {
      cap *= 2;
      char *nb = (char *)realloc(buf, cap);
      if (!nb) {
        free(buf);
        close(cfd);
        return NULL;
      }
      buf = nb;
    }
    ssize_t got = recv(cfd, buf + len, cap - len - 1, 0);
    if (got <= 0) {
      free(buf);
      close(cfd);
      return NULL;
    }
    len += (size_t)got;
    buf[len] = '\0';

    if (hdr_end < 0) {
      hdr_end = luke_http__hdr_end(buf, len);
      if (hdr_end < 0) {
        if (len > (1u << 20)) { /* 1 MiB header cap */
          free(buf);
          close(cfd);
          return NULL;
        }
        continue;
      }
      need_body = luke_http__content_length(buf, (size_t)hdr_end);
      if (need_body < 0) need_body = 0;
      if (need_body > (1L << 20)) { /* 1 MiB body cap */
        free(buf);
        close(cfd);
        return NULL;
      }
    }

    if (hdr_end >= 0 && (long)(len - (size_t)hdr_end) >= need_body) break;
  }

  /* Request line: METHOD SP path[?query] SP HTTP/... */
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

  LukeText body = luke_text("");
  if (need_body > 0) {
    body = luke_http__dup(a, buf + hdr_end, (size_t)need_body);
  }

  free(buf);

  LukeHttpRequest *req = (LukeHttpRequest *)luke_arena_alloc(a, sizeof(LukeHttpRequest), 8);
  req->method = method;
  req->path = path;
  req->query = query;
  req->body = body;
  req->client_fd = cfd;
  return req;
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

  char hdr[512];
  int hlen = snprintf(hdr, sizeof(hdr),
                      "HTTP/1.1 %d %s\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %zu\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      code, reason, ctype, body.len);
  if (hlen < 0 || (size_t)hlen >= sizeof(hdr)) {
    close(req->client_fd);
    req->client_fd = -1;
    return 0;
  }

  size_t sent = 0;
  while (sent < (size_t)hlen) {
    ssize_t n = send(req->client_fd, hdr + sent, (size_t)hlen - sent, 0);
    if (n <= 0) {
      close(req->client_fd);
      req->client_fd = -1;
      return 0;
    }
    sent += (size_t)n;
  }
  sent = 0;
  while (sent < body.len) {
    ssize_t n = send(req->client_fd, body.ptr + sent, body.len - sent, 0);
    if (n <= 0) {
      close(req->client_fd);
      req->client_fd = -1;
      return 0;
    }
    sent += (size_t)n;
  }

  close(req->client_fd);
  req->client_fd = -1;
  return 1;
}

/* ---------- SSE (Server-Sent Events) — keep connection open ---------- */

static inline int luke_http__send_all(int fd, const char *buf, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(fd, buf + sent, len - sent, 0);
    if (n <= 0) return 0;
    sent += (size_t)n;
  }
  return 1;
}

static inline int luke_http_sse_open(LukeHttpRequest *req) {
  if (!req || req->client_fd < 0) return 0;
  const char *hdr = "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/event-stream\r\n"
                    "Cache-Control: no-cache\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Connection: keep-alive\r\n"
                    "\r\n";
  if (!luke_http__send_all(req->client_fd, hdr, strlen(hdr))) {
    close(req->client_fd);
    req->client_fd = -1;
    return 0;
  }
  return 1;
}

static inline int luke_http_sse_data(LukeHttpRequest *req, LukeText data) {
  if (!req || req->client_fd < 0) return 0;
  if (!luke_http__send_all(req->client_fd, "data: ", 6)) goto fail;
  if (data.len && data.ptr) {
    if (!luke_http__send_all(req->client_fd, data.ptr, data.len)) goto fail;
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

#ifdef __cplusplus
}
#endif

#endif /* LUKE_NET_H */
