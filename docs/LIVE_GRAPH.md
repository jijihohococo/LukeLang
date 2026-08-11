# Live Graph — DB row → pixel

> **Status:** differential IVM + join recompute + scrub UI + causal resume green  
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
| **Differential triggers** | For simple `id = N` + single column: `NEW.col` / `OLD` — no full `group_concat` rewrite |
| Multi-join IVM | `WATCH … FROM db AS "SELECT … JOIN …"` — triggers on **both** tables recompute the cache |
| Causal log (`luke_ivm_log_*`) | Append on each pushed change |
| `Last-Event-ID` | Parsed on the request; `PUSH WATCH` replays log rows with `seq > id` |
| Scrub UI | Client-buffered history + Back / Forward / Live (`live_graph_scrub.luke`) |
| `data_version` gate | Idle beats skip even the cache read |

Acceptance: external `UPDATE` → pixel with `region=1`; reconnect resumes from the log; `watch_queries` stays bounded; join updates refresh without a full app refetch.

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

## Prior tiers

1. Spike — hand-rolled CDC poll → `region=1`
2. Server `WATCH` / `PUSH WATCH` surface
3. `PRAGMA data_version` gate
4. Trigger-maintained cache (`group_concat`)
5. NEW/OLD differential triggers + event-log resume
6. **Now:** multi-join cache recompute + client scrub UI beachhead

## What falls out next

1. **Free multicore parallelism** — independent reactions; compiler schedules without races
2. **Server+client scrub by log seq** — wire DevTools to `luke_ivm_log_*` (client buffer shipped)
3. **True multi-join differential dataflow** — delta propagation beyond recompute-on-both-tables

## Related

- [`STRATEGY.md`](./STRATEGY.md) — identity and phased plan
- [`REACTIVE.md`](./REACTIVE.md) — client reactive engine
- Spike A push: `SUBSCRIBE` + SSE — the wire primitive client `WATCH` sits on
