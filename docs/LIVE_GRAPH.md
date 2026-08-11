# Live Graph — DB row → pixel

> **Status:** tier 3 green (`data_version`-gated CDC)  
> **Thesis:** In LukeLang you never fetch, never invalidate, never subscribe, never diff. You declare dependencies once, and change finds its own way from row to pixel.

## One graph

```
DB row  →  server cell  →  [wire]  →  client cell  →  pixel
   └────────────── one dependency graph, compiler-known ──────────────┘
```

Mainstream stacks glue three separate worlds (DB / server / client) with queries, cache invalidation, refetching, subscriptions, and diffing. LukeLang's reactive engine already answers "when X changes, what needs to update?" inside the client. The Live Graph answers it for the **whole stack**.

## Tier 3 (this release)

**New:** `PUSH WATCH` only re-runs the SELECT when SQLite `PRAGMA data_version` changes — idle beats are free. Still catches out-of-process `UPDATE`s. Measurement: `watch_queries` stays tiny (≈2 for seed+Ada across 50 beats), not one query per beat.

| Piece | Role |
| --- | --- |
| `luke_db_data_version` | Cross-connection change detector |
| Gated `PUSH WATCH` | Query only on version bump; SSE push only on value change |
| Tier 2 surface | Unchanged — `WATCH … FROM db` + client `WATCH`/`BIND` |

Acceptance: external `UPDATE` → `name=Ada` → `region=1` **and** `watch_queries<=4` in the server log.

## Tier 2 (prior)

Server declares `WATCH user FROM db WHERE "id = 1"`; `PUSH WATCH` streams CDC. No hand-rolled `dbQuery` / SSE loop in app code.

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

## Spike 1 (prior)

Client `WATCH` + server hand-rolled poll — proved DB write → pixel with `region=1`.

## What falls out of the same graph (roadmap)

1. **Free multicore parallelism** — independent reactions have no shared deps; the compiler can schedule them without races.
2. **Time-travel the distributed app** — causality is recorded on both sides; rewind and "why did this pixel change?" back to the DB write.
3. **The database as a reactive cell** — true incremental view maintenance / differential dataflow (tier 3 is the cheap gate; full IVM is next).

Prove one tier at a time. Distributed concerns (reconnect, ordering, partial failure) get a real story before claiming production Live Graph.

## Related

- [`STRATEGY.md`](./STRATEGY.md) — identity and phased plan
- [`REACTIVE.md`](./REACTIVE.md) — client reactive engine
- Spike A push: `SUBSCRIBE` + SSE (`subscribe_cell_*`) — the wire primitive client `WATCH` sits on
