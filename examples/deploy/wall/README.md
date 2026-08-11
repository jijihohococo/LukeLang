# Wall app — one deployed Live Graph

**Wall sentence:** a Luke binary keeps a SQLite row hot; Caddy terminates TLS and
serves the browser client; `WATCH` / `PUSH WATCH` move the row to a pixel with
no hand-rolled fetch glue.

## Layout

| Path | Role |
|------|------|
| `server.luke` | Native HTTP + `WATCH` + `PUSH WATCH` on `:8800` |
| `client.luke` | Browser wasm — `WATCH /watch` + `BIND` |
| `Caddyfile` | TLS + `/assets` + reverse_proxy to Luke |
| `dist/` | `luke BUILD … -target browser` output |

## Local smoke (no TLS)

```bash
# from repo root
./scripts/wall_smoke.sh
```

## Production (Caddy)

```bash
export LUKE_TRUST_PROXY=1
export LUKE_AUTH_SECURE=1
export LUKE_SSE_ORIGIN=https://your.domain
vm/build/luke BUILD examples/deploy/wall/server.luke -o /var/luke/wall_server
vm/build/luke BUILD examples/deploy/wall/client.luke -target browser -o examples/deploy/wall/dist/client
# point Caddyfile root at examples/deploy/wall/dist
caddy run --config examples/deploy/wall/Caddyfile
```

See also [`docs/DEPLOY.md`](../../docs/DEPLOY.md).
