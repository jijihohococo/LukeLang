# Live Graph — DB row → pixel

> **Status:** tier 2 green (server `WATCH` + `PUSH WATCH`)  
> **Thesis:** In LukeLang you never fetch, never invalidate, never subscribe, never diff. You declare dependencies once, and change finds its own way from row to pixel.

## One graph

```
DB row  →  server cell  →  [wire]  →  client cell  →  pixel
   └────────────── one dependency graph, compiler-known ──────────────┘
```

Mainstream stacks glue three separate worlds (DB / server / client) with queries, cache invalidation, refetching, subscriptions, and diffing. LukeLang's reactive engine already answers "when X changes, what needs to update?" inside the client. The Live Graph answers it for the **whole stack**.

## Tier 2 (this release)

**Acceptance (unchanged chain):** external `UPDATE users SET name='…' WHERE id=1` → client BIND repaints exactly one node → `THE REGION PAINT COUNT == 1`.

**New:** the **server** declares the live cell — no hand-rolled `dbQuery` / SSE poll loop in app code.

| Piece | Role |
| --- | --- |
| `WATCH user FROM db WHERE "id = 1"` | Server: DB row → reactive TEXT cell (`SELECT name FROM users WHERE …`) |
| `PUSH WATCH user ON req FOR 50 BEATS EVERY 50 MILLISECONDS` | Compiler emits CDC poll + `httpSseData` on change |
| `WATCH user FROM "http://…/watch"` | Client: wire → same cell name (hides `SUBSCRIBE`) |
| `BIND "name" TO user` | Pixel depends on the cell |
| `live_graph_updater.luke` | External writer — graph spans processes |

Also supported: `WATCH user FROM db AS "SELECT name FROM users WHERE id = 1"` (explicit SQL).

Examples: `examples/build/live_graph_{server,client,updater}.luke` — asserted in `make test`.

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

CDC poll remains the tractable stand-in for incremental view maintenance. True IVM / differential dataflow is the next research-grade tier.

## Spike 1 (prior)

Client `WATCH` + server hand-rolled poll — proved DB write → pixel with `region=1` and zero client glue.

## What falls out of the same graph (roadmap)

1. **Free multicore parallelism** — independent reactions have no shared deps; the compiler can schedule them without races.
2. **Time-travel the distributed app** — causality is recorded on both sides; rewind and "why did this pixel change?" back to the DB write.
3. **The database as a reactive cell** — query results as live cells (incremental view maintenance / differential dataflow).

Prove one tier at a time. Distributed concerns (reconnect, ordering, partial failure) get a real story before claiming production Live Graph.

## Related

- [`STRATEGY.md`](./STRATEGY.md) — identity and phased plan
- [`REACTIVE.md`](./REACTIVE.md) — client reactive engine
- Spike A push: `SUBSCRIBE` + SSE (`subscribe_cell_*`) — the wire primitive client `WATCH` sits on
