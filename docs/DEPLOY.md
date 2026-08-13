# Deploying Luke HTTP (TLS via reverse proxy)

Luke’s `httpServe` / `SERVE ROUTES` binary speaks **plain HTTP** on a local port.
Production TLS and static assets belong in front — the Go/nginx pattern:

```
Internet → Caddy/nginx (TLS + static) → 127.0.0.1:PORT (Luke dynamic)
```

## Why not in-process TLS?

Interop and cert lifecycle are settled problems for Caddy/nginx/certbot.
Inventing a Luke TLS stack would add security surface without user-visible win.
Set `LUKE_AUTH_SECURE=1` so session cookies get the `Secure` flag behind HTTPS.

## Caddy example

```caddyfile
example.com {
  encode gzip
  handle /assets/* {
    root * /var/www/site
    file_server
  }
  handle {
    reverse_proxy 127.0.0.1:8800
  }
}
```

## nginx example

```nginx
server {
  listen 443 ssl http2;
  server_name example.com;
  ssl_certificate     /etc/ssl/certs/fullchain.pem;
  ssl_certificate_key /etc/ssl/private/privkey.pem;

  location /assets/ {
    root /var/www/site;
  }
  location / {
    proxy_pass http://127.0.0.1:8800;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
    proxy_set_header Connection "";
  }
}
```

## Trusting the proxy

When the binary sits behind a reverse proxy, set:

```bash
export LUKE_TRUST_PROXY=1
```

Then `ASK httpClientIp WITH req` uses the first `X-Forwarded-For` hop
(instead of the proxy’s loopback address).

## Runtime knobs

| Env / define | Default | Meaning |
|--------------|---------|---------|
| `LUKE_HTTP_TIMEOUT_MS` | 10000 | Idle connection timeout (event loop) / recv-send timeout (pool I/O) |
| `LUKE_HTTP_IO` | unset | `pool` → blocking worker recv/send (legacy) |
| `LUKE_HTTP_INLINE` | unset | `1` → run handlers on the loop thread (no queue) |
| `LUKE_HTTP_LOOPS` | #CPUs | SO_REUSEPORT event-loop count |
| `LUKE_HTTP_POOL_WORKERS` | 8 | Handler threads total (spread across loops) |
| `LUKE_HTTP_POOL_QUEUE` | 64 | Per-loop handler job queue depth |
| `LUKE_HTTP_BACKLOG` | 512 | `listen()` backlog |
| `LUKE_HTTP_KEEPALIVE_MAX` | 100000 | Max requests per keep-alive connection (`0` = unlimited) |
| `LUKE_TRUST_PROXY` | unset | `1` → honor `X-Forwarded-For` |
| `LUKE_AUTH_SECURE` | unset | `1` → `Secure` cookies |
| `LUKE_SSE_ORIGIN` | `*` | `Access-Control-Allow-Origin` for SSE (`PUSH WATCH`) |
| `LUKE_DB_POOL` | unset | `0` → real `sqlite3_close` (disable TLS pool) |

## Graceful shutdown

`httpServe` installs `SIGTERM` / `SIGINT` handlers. On signal it stops accepting,
closes idle sockets, drains in-flight handlers, joins workers, and returns so
your Luke program can finish (`SPEAK "done"` after `ASK httpServe`).

```bash
kill -TERM $(pidof your_luke_server)
```

## Keep-alive & chunked

- HTTP/1.1 keep-alive is honored (`Connection: keep-alive` / default). The
  event loop reads sockets; workers run only complete requests.
- Stream without `Content-Length`: `httpChunkOpen` / `httpChunk` / `httpChunkEnd`.

See [`BUILD_MODE.md`](./BUILD_MODE.md) and [`BACKEND_ROADMAP.md`](./BACKEND_ROADMAP.md).
