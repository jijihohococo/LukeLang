# Live Graph — DB row → pixel

> **Status:** spike 1 green (CDC poll)  
> **Thesis:** In LukeLang you never fetch, never invalidate, never subscribe, never diff. You declare dependencies once, and change finds its own way from row to pixel.

## One graph

```
DB row  →  server cell  →  [wire]  →  client cell  →  pixel
   └────────────── one dependency graph, compiler-known ──────────────┘
```

Mainstream stacks glue three separate worlds (DB / server / client) with queries, cache invalidation, refetching, subscriptions, and diffing. LukeLang's reactive engine already answers "when X changes, what needs to update?" inside the client. The Live Graph answers it for the **whole stack**.

## Spike 1 (this release)

**Acceptance:** `UPDATE users SET name='…' WHERE id=1` from any other process → client BIND repaints exactly one node → `THE REGION PAINT COUNT == 1` → client program has **no** query / fetch / subscribe / poll.

| Piece | Role today |
| --- | --- |
| SQLite row | Source of truth (`users.id = 1`) |
| Server CDC poll | Tractable stand-in for incremental view maintenance — re-`dbQuery`, `httpSseData` on change |
| `WATCH user FROM "url"` | Client sugar over SSE → reactive TEXT cell (hides `START SUBSCRIBE`) |
| `BIND "name" TO user` | Pixel depends on the cell |
| `live_graph_updater.luke` | External writer — proves the graph spans process boundaries |

Examples: `examples/build/live_graph_{server,client,updater}.luke` — asserted in `make test`.

### Client surface (war cry)

```luke
REMEMBER user AS ""
BIND "name" TO user
WATCH user FROM "http://127.0.0.1:8798/watch"
```

Server-side `WATCH user FROM db WHERE "id = 1"` (compiler-known query → push) and true incremental view maintenance come later. Spike 1 keeps the **client** free of glue and proves the causal chain with polling CDC on the existing SSE plumbing.

## What falls out of the same graph (roadmap)

1. **Free multicore parallelism** — independent reactions have no shared deps; the compiler can schedule them without races.
2. **Time-travel the distributed app** — causality is recorded on both sides; rewind and "why did this pixel change?" back to the DB write.
3. **The database as a reactive cell** — query results as live cells (incremental view maintenance / differential dataflow).

Prove one tier at a time. Distributed concerns (reconnect, ordering, partial failure) get a real story before claiming production Live Graph.

## Related

- [`STRATEGY.md`](./STRATEGY.md) — identity and phased plan
- [`REACTIVE.md`](./REACTIVE.md) — client reactive engine
- Spike A push: `SUBSCRIBE` + SSE (`subscribe_cell_*`) — the wire primitive `WATCH` sits on
