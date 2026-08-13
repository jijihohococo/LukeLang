# LukeLang Backend Benchmarks

> **Scope:** HTTP serving and database-backed API throughput, LukeLang vs Go.
> **Posture:** these numbers are honest, reproducible, and caveated. Read
> [§2 Environment](#2-environment) and [§8 Limitations](#8-limitations) before quoting any figure.
> Numbers are representative single 5-second samples on one machine; expect ±10% run-to-run and
> re-run locally before citing.

## 1. Summary

| Workload | LukeLang | Go (idiomatic) | Result |
|---|---|---|---|
| HTTP, trivial handler | 118k–180k req/s | 97k–143k (`net/http`) | **Luke +12–42%** |
| SQLite read API (JSON) | 105k–138k | 33k–58k (`database/sql`) | **Luke 2.4–3×** |
| SQLite write API | 17k–27k | 11k–13k | **Luke ~2×** (integrity verified) |
| SQLite large/cold (20M rows) | 26k cold / 48k warm | 8k–24k | **Luke 1.5–3×** |
| **Postgres read API** | **33k–43k (blocking)** | 15k–44k (pgx) | **Luke wins at low/mid concurrency, ties at high** |
| Postgres, async-pipeline mode | 21k–31k | — | **Underperforms blocking — see §7** |

**Headline, stated honestly:** for the **embedded-SQLite** backend, LukeLang is decisively faster
than idiomatic Go (2–3× across reads, writes, and large datasets). For **networked Postgres**,
LukeLang's *blocking* driver beats or ties Go's `pgx`; its *async-pipelined* driver — the
theoretically faster design — does **not** win on localhost and is currently a net negative there.

## 2. Environment

| | |
|---|---|
| CPU | 4 cores |
| RAM | 15 GiB |
| OS | Linux (x86-64) |
| Go | 1.24 / 1.25, `GOMAXPROCS=4` |
| SQLite | native C library (`-lsqlite3`), WAL, `busy_timeout=5000`, `synchronous=NORMAL`, `mmap` |
| Postgres | PostgreSQL 16, local, `--auth=trust` |
| Topology | **server, load client, and database all co-resident on the same 4 cores** |
| Network | loopback (µs RTT) — see the caveat this creates in §7–§8 |

The single-box topology is the most important caveat: the load generator competes with the server
(and, for Postgres, the database) for the same 4 CPUs, which lowers absolute numbers for everyone.
**Ratios are fair** — both sides face identical conditions — but absolute figures are not a
dedicated-hardware ceiling.

## 3. Method

- **Load generator:** a custom Go client holding *C* concurrent keep-alive connections, each
  issuing requests back-to-back for a fixed 5 s, **reconnecting on close** (so a server's
  keep-alive cap cannot silently inflate its number). A request counts only after its full
  response body is read. Random-key variant used for the large-dataset test.
- **Handlers are matched:** identical route, identical query, equivalent JSON response, same
  database and dataset for both LukeLang and Go.
- **Baselines are idiomatic Go:** `net/http` for the HTTP layer; `database/sql` with the standard
  driver for each database (`modernc.org/sqlite` and `mattn/go-sqlite3` for SQLite;
  `jackc/pgx` for Postgres). These are what "vs Go" means for the median engineer; a
  hand-tuned Go bypassing the standard library could narrow some gaps.
- **Every DB result is gated on a correctness probe first** (below). A throughput number is only
  reported for a configuration that first passed correctness.

## 4. HTTP layer (trivial handler)

Handler returns a fixed `"ok"` — isolates accept/parse/route/respond from any application work.

| Concurrency | LukeLang | Go `net/http` |
|---:|---:|---:|
| 50 | 118,103 | 99,377 |
| 200 | 137,520 | 97,166 |
| 500 | 149,992 | 133,888 |
| 1000 | 180,396 | 143,077 |

LukeLang's HTTP layer (SO_REUSEPORT event loop, one loop per core; keep-alive; `writev`;
`TCP_NODELAY`; `accept4`/`EPOLLET`) is 12–42% ahead of Go's standard server and scales past
1000 concurrent connections without collapse.

## 5. SQLite — read API

`GET /user/:id` → parameterized `SELECT` → JSON. LukeLang uses a thread-local connection pool +
prepared-statement cache; Go uses `database/sql` with `SetMaxOpenConns(8)`.

| Concurrency | LukeLang | Go `modernc` (pure-Go) | Go `mattn` (cgo C-sqlite) |
|---:|---:|---:|---:|
| 50 | 104,944 | 33,328 | 25,417 |
| 200 | 138,087 | 48,237 | 28,608 |
| 500 | 137,850 | 57,711 | 29,903 |

LukeLang is 2.4–3× Go's *best* SQLite option. Notably `mattn` (cgo, real C SQLite) is Go's
*slowest* here — the cgo boundary crossing per query outweighs the native engine. LukeLang calls
C SQLite directly (no cgo, no GC), so its DB→JSON throughput matches its own bare-HTTP number —
the database has stopped being the bottleneck.

## 6. SQLite — writes and large/cold data

**Write API** (`UPDATE` per request, max WAL contention). Both plateau because WAL serializes
writers; write integrity was verified separately (exactly 5000 concurrent increments produced a
final counter of exactly 5000 — no lost writes under contention).

| Concurrency | LukeLang | Go |
|---:|---:|---:|
| 10 | 17,238 | 11,169 |
| 50 | 26,308 | 13,471 |
| 200 | 26,747 | 12,743 |

**Large / cold dataset** (20M rows, 867 MB; random-id lookups; OS cache dropped before each cold run):

| | LukeLang | Go `modernc` | Go `mattn` |
|---|---:|---:|---:|
| cold (cache dropped) | 25,828 | 17,587 | 7,713 |
| warm | 48,303 | 23,901 | 20,439 |

LukeLang leads throughout, though the gap narrows cold vs warm as disk I/O begins to dominate.
**Not truly disk-bound:** 867 MB fits in 15 GiB RAM, so this measures deep-B-tree random access
with a cold-cache ramp, not sustained disk saturation (which would need a dataset larger than RAM).

## 7. Postgres — networked database

Real PostgreSQL 16, `GET /user/:id` → parameterized `SELECT` → JSON. LukeLang driver is libpq-based
with a thread-local pool + prepared statements (blocking mode) or a shared async pipelined executor
(the default). Go uses `database/sql` + `jackc/pgx`, `SetMaxOpenConns(8)`. Correctness probe (300
concurrent distinct-id requests, 0 wrong rows) passed for LukeLang before any number below.

**Blocking driver** (`LUKE_PG_ASYNC=0`, handler pool 64):

| Concurrency | LukeLang (blocking) | Go `pgx` |
|---:|---:|---:|
| 50 | 33,333 | 15,332 |
| 200 | 40,764 | 33,850 |
| 500 | 43,330 | 43,596 |

**Async pipelined executor** (default, `LUKE_PG_CONNS=16`, handler pool 64):

| Concurrency | LukeLang (async) | Go `pgx` |
|---:|---:|---:|
| 50 | 27,477 | 15,280 |
| 200 | 31,209 | 34,899 |
| 500 | 23,825 | 42,852 |

**Two honest findings:**

1. **LukeLang's *blocking* driver beats or ties Go `pgx`** — 2.2× at concurrency 50, 1.2× at 200,
   parity at 500 — and it scales with concurrency (33k → 41k → 43k). So LukeLang *can* beat Go on
   Postgres, via the simple pooled blocking driver plus a large handler pool.

2. **The async pipelined executor — the design intended to *beat* Go — does not win here.** It is
   slower than the blocking driver and loses to Go above concurrency 200, and its throughput
   *degrades* as concurrency rises. On loopback the round-trip is microseconds, so there is almost
   no latency to amortize by pipelining, while the executor's single dispatch thread, submit-queue
   mutex, and per-completion wakeup add real overhead. Cross-request pipelining is a bet on
   **network latency**; on localhost that bet loses. Whether it pays off over a real WAN link
   (millisecond RTT) is **untested** — this environment has no way to inject network latency —
   and remains an open question. **Recommendation:** default Postgres to the blocking driver
   (`LUKE_PG_ASYNC=0`) until the async path is shown to win under real RTT, and revisit the
   single-dispatch-thread design if it is.

## 8. Limitations

- **Single 4-core box**, server + client (+ Postgres) sharing CPU. Ratios are fair; absolute
  numbers are not a dedicated-hardware ceiling.
- **Idiomatic-Go baselines** (`net/http`, `database/sql`). Hand-tuned Go (e.g. `fasthttp`, a driver
  used directly without `database/sql`, or `pgx` in explicit pipeline mode) could narrow or, in
  some cases, close a gap.
- **Localhost networking only.** The Postgres numbers do not exercise real network latency, which
  is precisely the regime the async pipeline targets — so the pipeline's value proposition is
  neither confirmed nor refuted here.
- **Trivial handlers.** Real handlers doing more work per request would shift results toward the
  application logic and away from the I/O and DB layers measured here.
- **Single samples.** Representative, not medians; expect run-to-run variance.

## 9. Reproduction

Servers are minimal matched programs: a LukeLang `.luke` and a Go equivalent per workload, each
returning the same JSON for `GET /user/:id` against the same seeded dataset. Load is the keep-alive
Go client described in §3 at concurrency 50/200/500 for 5 s. SQLite uses the native library; the
Postgres run needs a local PostgreSQL 16 and `libpq`. Tuning knobs referenced above:
`LUKE_HTTP_POOL_WORKERS`, `LUKE_HTTP_LOOPS`, `LUKE_PG_ASYNC`, `LUKE_PG_CONNS`, `LUKE_PG_POOL`.

## 10. Bottom line

- **Embedded SQLite:** LukeLang is genuinely and consistently faster than idiomatic Go — reads,
  writes, and large datasets — because native C SQLite + arena + no-GC + the faster HTTP layer
  compound, and the connection pool removes the per-request DB cost.
- **Networked Postgres:** LukeLang's blocking driver beats or ties Go; the capability is real and
  correct. The async-pipelined "faster than Go" design does not deliver on localhost and awaits a
  real-latency test before it can be claimed.
- **Overall:** "LukeLang's backend is faster than Go" is **true and proven for the SQLite class**,
  and **true for Postgres via the blocking driver at low-to-mid concurrency** — not a blanket claim
  across every workload, and explicitly not yet demonstrated for the async pipeline.
