# LukeLang Networked Database Driver — Implementation Spec

> Status: spec / not yet built. Target: a Postgres driver that gives LukeLang a
> networked-DB backend, and — via async pipelining on the existing event loop —
> makes it faster than idiomatic Go (`database/sql`) on that workload.

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

## 3. Phase 2 — Async pipelined executor (this is what beats Go)

Phase 1's ceiling is that a handler thread blocks for a whole round-trip, so concurrency =
handler-pool size and every query pays a full RTT. Phase 2 keeps the **handler API
synchronous** (no language change) but replaces the per-thread blocking connections with a
shared async executor underneath.

### 3.1 Design

```
handler threads                     DB I/O thread(s)                 Postgres
  (HTTP pool)                    (own epoll, M connections)
      │  submit(query, params) ───────► MPSC queue                        
      │  block on completion            │ batch ready queries             
      │                                 ├─ PQsendQueryPrepared × k ──────► (pipeline: k queries,
      │                                 │  (PQ pipeline mode, no wait)      one network write)
      │                                 │ epoll PQsocket for readable       
      │  ◄── signal(result) ────────────┤ PQconsumeInput / PQgetResult ◄── (k results, ~1 RTT)
      ▼                                 (FIFO-match result → waiter)
  finish response
```

- **A dedicated DB I/O layer**: a small pool of `M` persistent `PGconn*` (sized to the
  Postgres connection budget, e.g. 16–64), owned by **one or two DB I/O threads**, each
  running an epoll loop. Build these with the same epoll helpers the HTTP loop uses — model
  the thread on `luke_http__loop_thread` (`luke_net.h:2135`) and register `PQsocket(conn)`
  fds exactly like HTTP conn fds (`EPOLLIN`/`EPOLLOUT`, `EAGAIN` handling already exists).
- **Submit + block, not spin.** A handler calls `pgQueryBind`; internally it enqueues a
  request (SQL id + params + a completion slot) onto an MPSC queue and blocks on a per-request
  completion (futex/`eventfd`/condvar). The handler thread is parked, not spinning.
- **Cross-request pipelining.** The DB I/O thread drains the queue and, per connection,
  enters pipeline mode (`PQenterPipelineMode`, libpq ≥14) and issues several queued queries
  back-to-back with `PQsendQueryPrepared` before flushing — so `k` queries from `k` different
  handlers cost ~one RTT on that connection instead of `k`. Responses return in FIFO order
  per connection; match each result to its waiter and signal it.
- **libpq async, no cgo boundary, no GC.** `PQsendQuery*` + `PQconsumeInput` + `PQgetResult`
  driven off `PQsocket` readability. This is the same integration Go's fastest driver (pgx)
  does — but LukeLang pays no cgo call cost per libpq call and no GC per result.

### 3.2 Why this beats idiomatic Go
- `database/sql` issues **one query per connection per round-trip** — no pipelining. Its
  throughput ceiling is `pool_size / RTT`.
- The executor's ceiling is `(M connections × pipeline_depth) / RTT`. With depth ≫ 1 that is
  multiples higher on the same connection budget and the same Postgres.
- Against **pgx with pipelining** the fight is closer and decided by client overhead — where
  LukeLang's no-cgo / no-GC / native-arena path has the edge, the same way it did on SQLite.

### 3.3 Correctness requirements (call these out in review)
- **Pipeline FIFO matching.** Postgres returns pipelined results strictly in send order per
  connection. The executor must dequeue waiters for a connection in the exact order it sent
  their queries. An off-by-one here returns *another request's row* — the highest-severity
  bug class. Gate CI on a concurrent distinct-id probe that would catch a mis-match.
- **Error isolation in a pipeline.** One failing query in a pipeline puts the connection into
  an aborted state until sync; handle `PQpipelineSync` boundaries and fail only the offending
  waiter, not its neighbors.
- **Connection loss / reconnect.** On `PQstatus == CONNECTION_BAD`, drain that connection's
  in-flight waiters with a retryable error and re-establish; never leak a blocked handler.
- **Backpressure.** Bound the submit queue; when full, either block the submitter or fail
  fast with a clear error — do not grow unboundedly under load.
- **Transactions.** A `BEGIN…COMMIT` sequence must pin to a single connection for its
  duration (pipelining across a transaction boundary on shared connections is unsafe). Expose
  a "checked-out connection" mode for transactional handlers.

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
- **Servers:** Luke `pg_api` (Phase 2) vs Go `net/http` + `database/sql`/pgx, same schema,
  same parameterized `SELECT … WHERE id = $1`, same JSON shape.
- **Baselines:** Go `database/sql` (idiomatic) **and** pgx-with-pipeline (Go's best) — beat
  the first, be competitive with the second.
- **Load:** the random-id generator already built, at concurrency 50 / 200 / 500.
- **Parity floor:** ≥ Go `database/sql`.
- **"Beat Go" bar:** exceed Go `database/sql` at conc ≥ 200, driven by pipeline depth > 1.
- **Correctness gate (must pass before any throughput number counts):** concurrent
  distinct-id probe returns every request's own row, 0 mismatches; write-integrity probe
  (exact N increments → counter == N) as done for SQLite.

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
