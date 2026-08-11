# Live Graph — DB row → pixel

> **Status:** true keyed differential IVM (single-table + equi-JOIN) + scrub UI + causal resume + wire fail-closed  
> **Thesis:** In LukeLang you never fetch, never invalidate, never subscribe, never diff. You declare dependencies once, and change finds its own way from row to pixel.

## One graph

```
DB row  →  server cell  →  [wire]  →  client cell  →  pixel
   └────────────── one dependency graph, compiler-known ──────────────┘
```

Mainstream stacks glue three separate worlds (DB / server / client) with queries, cache invalidation, refetching, subscriptions, and diffing. LukeLang's reactive engine already answers "when X changes, what needs to update?" inside the client. The Live Graph answers it for the **whole stack**.

## This release

| Piece | Role |
| --- | --- |
| `WATCH … FROM db WHERE …` | Declares a DB-backed reactive cell |
| Trigger IVM cache (`luke_ivm_*`) | Maintained TEXT snapshot |
| **Differential triggers** | Single-table `id = N`: `NEW.col` / `OLD` — no full `group_concat` rewrite |
| **Keyed join differential** | `JOIN … ON a.x = b.y WHERE a.x = N` → `WHEN NEW` on both tables + partner probe (not recompute-both) |
| Causal log (`luke_ivm_log_*`) | Append on each pushed change |
| `Last-Event-ID` | Parsed on the request; `PUSH WATCH` replays log rows with `seq > id` |
| Wire hardening | SSE send failure aborts the stream; `LUKE_SSE_ORIGIN`; scoped PUSH requires user |
| Scrub UI | Client-buffered history + Back / Forward / Live (`live_graph_scrub.luke`) |
| `data_version` gate | Idle beats skip even the cache read |
| Wall app | `examples/deploy/wall` — one deployable DB→pixel binary + Caddy |

Acceptance: external `UPDATE` → pixel with `region=1`; reconnect resumes from the log; `watch_queries` stays bounded; keyed join updates fire only for matching keys.

### War cry surface

```luke
# server
WATCH user FROM db WHERE "id = 1"
PUSH WATCH user ON req FOR 50 BEATS EVERY 50 MILLISECONDS

# client
REMEMBER user AS ""
BIND "name" TO user
WATCH user FROM "http://127.0.0.1:8798/watch"
```

Join shape that gets true differential (not recompute):

```luke
WATCH card FROM db AS "SELECT u.name || ' · ' || p.title FROM users u JOIN profiles p ON u.id = p.user_id WHERE u.id = 1"
```

## Prior tiers

1. Spike — hand-rolled CDC poll → `region=1`
2. Server `WATCH` / `PUSH WATCH` surface
3. `PRAGMA data_version` gate
4. Trigger-maintained cache (`group_concat`)
5. NEW/OLD differential triggers + event-log resume
6. Multi-join cache recompute + client scrub UI beachhead
7. **Now:** keyed equi-join differential + wire fail-closed + wall deploy proof

## What falls out next

1. **Free multicore parallelism** — independent reactions; compiler schedules without races
2. **Server+client scrub by log seq** — wire DevTools to `luke_ivm_log_*` (client buffer shipped)
3. Broader differential shapes — multi-row, LEFT JOIN, non-key filters (still recompute or skip IVM)

## Related

- [`STRATEGY.md`](./STRATEGY.md) — identity and phased plan
- [`REACTIVE.md`](./REACTIVE.md) — client reactive engine
- [`DEPLOY.md`](./DEPLOY.md) — TLS / Caddy / `LUKE_SSE_ORIGIN`
- Wall app: [`examples/deploy/wall/`](../examples/deploy/wall/)
- Spike A push: `SUBSCRIBE` + SSE — the wire primitive client `WATCH` sits on
