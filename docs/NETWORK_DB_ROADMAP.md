# LukeLang Networked Database Driver — Implementation Spec

> Status: **Phase 1 shipped (default). Slipstream (Phase 2 rewrite) behind `LUKE_PG_ASYNC=1`.**
> Blocking libpq driver with TLS pool + stmt cache is the honest default (beats Go on
> localhost). Slipstream shards across cores, drops the global submit mutex, and drafts
> into pipelines only when queue depth ≥ `LUKE_PG_DRAFT_MIN` — so it ties blocking when
> there is nothing to amortize and pulls ahead under real RTT. Flip to default only after
> localhost parity + injected-latency acceptance (`scripts/luke_pg_slipstream_cmp.py`).
>
> Escape hatches / knobs: `LUKE_PG_ASYNC=1` (Slipstream), `LUKE_PG_POOL=0`,
> `LUKE_PG_CONNS`, `LUKE_PG_SHARDS`, `LUKE_PG_PIPELINE_DEPTH`, `LUKE_PG_DRAFT_MIN`.
> Transaction pin: `pgCheckout` / `pgCheckin`.

## 0. Why this exists

LukeLang's stdlib has only `sqlite.luke` — no networked database. The embedded-SQLite
path is already 2–3× faster than Go (see benchmarks), but most production backends run
on Postgres, and LukeLang cannot connect to one at all. This closes that gap, and does it
on the axis where a client can actually beat another on a networked DB.

**The performance thesis.** On a networked DB every query is a round-trip; the RTT and the
Postgres server's work are identical for LukeLang and Go, so per-query speed cannot win.
The only axes a client controls are **concurrency efficiency** and **pipelining**. Postgres
itself bounds useful connection counts (default `max_connections` ~100), so this is *not* a
goroutine-scale concurrency race — it is a *use a bounded connection set efficiently* race.
The winning move is **cross-request pipelining**: batch queries from many concurrent handlers
onto a few connections and amortize the RTT. Idiomatic Go (`database/sql`) does **not**
pipeline — one query per connection per round-trip. LukeLang, driving libpq's async +
pipeline mode from the `SO_REUSEPORT` event loop that already beats Go's netpoller on HTTP,
and paying neither a cgo boundary nor GC per query, can beat it. That is the whole plan.

## 1. Shape of the work (three phases)

| Phase | Deliverable | Beats Go? |
|------|-------------|-----------|
| 1 | Blocking libpq driver — capability | No — parity at best; capped by handler-thread count |
| 2 | Shared **async pipelined executor** — the win | **Yes** vs `database/sql`; competitive vs pgx-pipeline |
| 3 | Fully async handlers (continuations) — optional | Removes the handler-thread ceiling; out of scope here |

Ship Phase 1 first for correctness and capability, then layer Phase 2 underneath it without
changing the language surface.

---

## 2. Phase 1 — Blocking libpq driver (capability)

Mirror the SQLite driver exactly, one layer up. Nothing here is novel; it is the same pattern
already proven in `vm/runtime/luke_db.h` + `vm/stdlib/sqlite.luke`.

### 2.1 Files
- **`vm/runtime/luke_pg.h`** — C wrapper over libpq, analogous to `luke_db.h`.
- **`vm/stdlib/pg.luke`** — Luke-facing API, analogous to `sqlite.luke`.
- **`vm/src/build_c.cpp`** — map `__luke_pg_*` native helpers (near the other native-call
  mappings), and add `"pq"` to `r.linkLibs` when `std/pg` is imported — the exact spot where
  `sqlite3` is pushed today is `build_c.cpp:7730`. Copy that block.

### 2.2 API surface (`pg.luke`)
Keep it identical in spirit to `sqlite.luke` so it is familiar and injection-safe:
- `pgOpen WITH connstr` → connection handle. `connstr` is a standard libpq conninfo /
  URI (`postgres://user:pass@host:5432/db?sslmode=require`). libpq owns auth (SCRAM),
  TLS, and networking.
- `pgExecBind WITH conn, sql, params` → rows affected. Parameterized via `PQexecParams`
  (`$1,$2,…`), **never** string interpolation — same guarantee as `dbExecBind`.
- `pgQueryBind WITH conn, sql, params` → first column of first row (scalar), mirroring
  `dbQueryBind`.
- `pgRowsBind WITH conn, sql, params` → a rowset accessor (follow whatever `sqlite.luke`
  exposes for multi-row reads; match it).
- `pgClose WITH conn`.

### 2.3 Runtime (`luke_pg.h`) — reuse every DB lesson already landed
- **Thread-local connection pool.** Same design as `luke_db.h`'s `__thread` pool
  (`luke_db.h:146`): one `PGconn*` per worker thread per conninfo, opened once, reused;
  `pgClose` is a refcount/no-op, real `PQfinish` only on thread teardown. `LUKE_PG_POOL=0`
  disables, matching `LUKE_DB_POOL`.
- **Prepared-statement cache.** Per connection, keyed by SQL string, using `PQprepare` +
  `PQexecPrepared` — the analogue of the SQLite stmt cache (`luke_db.h:191`). This is a big
  win on Postgres because it skips server-side re-planning.
- **Parameterized only.** Bind Luke values as libpq params (text or binary format);
  injection-safe by construction.
- **Error path.** `PQresultStatus` / `PQerrorMessage` → the same failure surface SQLite
  errors take today.

### 2.4 Phase-1 acceptance
- A `pg_api.luke` example: `GET /user/:id` → `pgQueryBind SELECT name … WHERE id = $1`
  → JSON reply, structurally identical to the SQLite `backend_api.luke`.
- Correctness under concurrency: N concurrent distinct-id lookups each return their own
  row, 0 mismatches (the same adversarial check used for the SQLite pool).
- CI can run against a Postgres in a container/service; gate on the correctness probe.

Phase 1 gives LukeLang a real Postgres backend. It will land near parity with Go, not ahead —
blocking `PQexec` on the handler pool caps in-flight queries at the handler-thread count.

---

## 3. Slipstream — adaptive sharded pipelined executor

Phase 1's ceiling is that a handler thread blocks for a whole round-trip, so concurrency =
handler-pool size and every query pays a full RTT. Slipstream keeps the **handler API
synchronous** (no language change) but replaces per-thread blocking connections with a
sharded async executor underneath — and only pays pipeline coordination when there is a
batch waiting.

### 3.1 Design (fixes the three Phase-2 defects)

Earlier Phase-2 used one dispatch thread, one global submit mutex, and unconditional
pipelining — and lost to blocking on localhost. Slipstream:

1. **Shard across cores.** `S = LUKE_PG_SHARDS` (default `sysconf(_SC_NPROCESSORS_ONLN)`),
   each shard owns its epoll thread, `LUKE_PG_CONNS/S` connections, and submit queue.
   Handlers pick `worker_thread_index % S` (stable TLS index).
2. **Per-shard submit.** No global `qmu`. Per-shard mutex (v1; MPSC later). Wake the
   shard's eventfd only on a 0→non-empty edge.
3. **Opportunistic drafting.** Drain the shard queue; for each idle connection take up to
   `PIPELINE_DEPTH` waiters. If `count < DRAFT_MIN` (default 2): `PQsendQueryPrepared` with
   **no** pipeline mode. Else: `PQenterPipelineMode` → send × count → `PQpipelineSync`.
   Queue depth is the batch size — self-tuning, no flag for "use pipeline".

```
handler threads                     Slipstream shards (S)              Postgres
  (HTTP pool)                    (own epoll, conns/S each)
      │  submit → shard queue ───────► per-shard MPSC/mutex
      │  block on own condvar           │ opportunistic draft
      │                                 ├─ count < DRAFT_MIN: send alone
      │                                 └─ else: pipeline × k + sync ──► ~1 RTT for k
      │  ◄── signal(result) ────────────┤ drain all ready in one wakeup
  finish response
```

### 3.2 Why this beats idiomatic Go (under latency)
- `database/sql` issues **one query per connection per round-trip** — no pipelining.
- Slipstream's ceiling is `(M connections × pipeline_depth) / RTT` when the queue is deep;
  on localhost / shallow queues it takes the count==1 path and tracks blocking.
- Against **pgx with pipelining** the fight is closer and decided by client overhead.

### 3.3 Correctness requirements (CI-gated)
- **Pipeline FIFO matching.** Dequeue waiters in exact send order per connection. Distinct-id
  probe at conc 50/200/300 under `LUKE_PG_ASYNC=1`.
- **Error isolation.** Fail the errored waiter; `PGRES_PIPELINE_ABORTED` neighbors are
  requeued after sync so they stay alive.
- **Reconnect.** On `CONNECTION_BAD`, fail in-flight waiters and re-establish — never leak.
- **Transactions.** `pgCheckout` / `pgCheckin` pin a dedicated `PGconn` — never drafted.

---

## 4. Phase 3 — Fully async handlers (optional, later)

Phase 2 still blocks a handler thread per in-flight request, so HTTP concurrency is bounded by
the handler pool. Removing that entirely means handlers must **suspend** at a DB call and
resume on completion — i.e. stackful coroutines or a compiler-driven continuation transform.
That is a language-runtime change (effectively async/await) and is out of scope here. Note it
as the theoretical ceiling; Phase 2 already beats idiomatic Go without it, because Postgres's
own connection limit means goroutine-scale request concurrency is not the bottleneck.

---

## 5. Benchmark & acceptance (how we prove the claim)

Reuse the existing Go-vs-Luke harness, pointed at a real Postgres:
- **Servers:** Luke `pg_api` (Slipstream) vs Go `net/http` + `database/sql`/pgx, same schema,
  same parameterized `SELECT … WHERE id = $1`, same JSON shape.
- **Baselines:** Go `database/sql` (idiomatic) **and** pgx-with-pipeline (Go's best) — beat
  the first, be competitive with the second.
- **Load:** the random-id generator already built, at concurrency 50 / 200 / 500.
- **Localhost gate:** Slipstream ≥ blocking at 50/200/500 (count==1 fast path).
- **Latency gate (required before default):** under `tc … netem delay 2ms` (or remote PG),
  Slipstream beats blocking and beats Go pgx — `scripts/luke_pg_slipstream_cmp.py --latency-ms 2`.
- **Correctness gate:** concurrent distinct-id probe, 0 mismatches, under Slipstream at every
  concurrency level CI runs.

## 6. Honest ceiling (state this alongside any result)

Even done perfectly, the networked-DB win is **narrower than the embedded-SQLite win** and
appears **only where the client is the bottleneck** — high concurrency and/or pipelined
batches. A low-concurrency, one-query-per-request app is RTT-bound and ties Go; nobody beats
the network. And against a networked Postgres, the DB server and the wire are shared floors.
The value of Phase 1 is **capability** (LukeLang can run the stack real apps use); the value of
Phase 2 is a **genuine but bounded** throughput edge over idiomatic Go on concurrent workloads.

## 7. Sequencing summary

1. **Phase 1** — libpq blocking driver (`luke_pg.h` + `pg.luke` + `linkLibs "pq"` at
   `build_c.cpp:7730`), thread-local pool + prepared-stmt cache. Capability + correctness.
2. **Phase 2** — shared async pipelined executor on its own epoll thread(s), modeled on
   `luke_http__loop_thread`, using libpq async + `PQenterPipelineMode`; synchronous handler
   API unchanged. This is the part that beats Go.
3. **Benchmark** against Go `database/sql` and pgx with a real Postgres; gate on the
   correctness probes before trusting any throughput number.
