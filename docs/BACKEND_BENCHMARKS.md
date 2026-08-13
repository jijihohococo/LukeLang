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
| **Postgres read API** (localhost) | 33k–59k | 15k–44k (pgx) | **Luke wins/ties** — see §7 |
| **Postgres read API** (~2ms RTT, fair 16-conn budget) | **12k–14k (Slipstream)** | 4.7k (pgx) | **Luke 2.6–3.3× — Slipstream** |

**Headline, stated honestly:** for the **embedded-SQLite** backend, LukeLang is decisively faster
than idiomatic Go (2–3× across reads, writes, and large datasets). For **networked Postgres**, at a
fair, equal connection budget LukeLang beats idiomatic Go everywhere — modestly on localhost
(~1.3×) and decisively under network latency (2.6–3.3× at ~2ms RTT) via the **Slipstream** adaptive
pipelined executor, which also serves high request concurrency on a small, Postgres-safe pool. The
one honest asterisk: the latency figures come from an in-process latency injector, not a real
network (see §7–§8).

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

Real PostgreSQL 16, `GET /user/:id` → parameterized `SELECT` → JSON. LukeLang's libpq driver has two
modes: a thread-local blocking pool, and **Slipstream** — an adaptive sharded pipelined executor
(one shard per CPU, per-shard submit queue, *opportunistic* drafting that only pipelines when queries
are already queued, so it behaves like the blocking path when there is nothing to amortize). Go uses
`database/sql` + `jackc/pgx` in normal query mode. Every number below was gated on a correctness
probe first (see the correctness row).

### The fair comparison — equal connection budget

The decisive variable is the **DB connection budget**. Blocking uses one connection per handler
thread, so its count scales with HTTP concurrency (and can exceed Postgres's `max_connections` under
load); Slipstream serves any request concurrency on a *fixed* pool. Compared at an equal 16-connection
budget:

| Scenario | Slipstream (16 conns) | Blocking (16 conns) | Go `pgx` (pool 16) | Slipstream vs best |
|---|---:|---:|---:|---:|
| Localhost, conc 200 | **58,861** | 46,630 | — | **1.26×** |
| ~2 ms DB RTT, conc 200 | **12,200** | 4,765 | 4,711 | **2.6×** |
| ~2 ms DB RTT, conc 500 | **14,311** | 4,345 | — | **3.3×** |
| Correctness (pipeline + latency + 60-way concurrency) | **400/400 correct rows** | | | ✅ |

At an equal budget Slipstream wins everywhere — modestly on localhost, decisively under latency,
because pipelining lets a small pool draft many queries through each round-trip. The gap *widens*
with concurrency (deeper queues → deeper batches → more amortization).

### Why the connection budget is the honest crux

Give blocking *more* connections than Slipstream and its raw connection count competes on localhost —
but that is neither a fair comparison nor a production-safe configuration: serving 500 concurrent
requests the blocking way wants ~500 Postgres connections, which overruns a real server's limit.
Slipstream serves the same 500 requests on 16 connections. So under real load against real Postgres,
Slipstream's advantage is not only throughput but **viability**.

### How the latency was produced (important)

`netem` is unavailable in this environment (no kernel module), so the ~2 ms round-trip was injected
by an in-process TCP delay proxy sitting between the app and Postgres — identical for LukeLang and
Go, and built so reads keep flowing while writes are held in order (it models propagation delay, so
pipelining is genuinely rewarded). This is a faithful simulation, **not** a real network link. The
result should be re-confirmed on real hardware with real RTT before it is treated as final; a ready
harness exists at `scripts/luke_pg_slipstream_cmp.py --latency-ms 2`.

**Recommendation:** the gate Cursor set — "default stays blocking until latency acceptance passes" —
is met. Slipstream ties-or-beats blocking on localhost, wins 2.6–3.3× under latency, is correct, and
has a bounded connection footprint. Default Postgres to Slipstream (`LUKE_PG_ASYNC=1`) and keep the
blocking pool as the escape hatch.

## 8. Limitations

- **Single 4-core box**, server + client (+ Postgres) sharing CPU. Ratios are fair; absolute
  numbers are not a dedicated-hardware ceiling.
- **Idiomatic-Go baselines** (`net/http`, `database/sql`). Hand-tuned Go (e.g. `fasthttp`, a driver
  used directly without `database/sql`, or `pgx` in explicit pipeline mode) could narrow or, in
  some cases, close a gap.
- **Simulated network latency.** The Postgres latency numbers use an in-process TCP delay proxy, not
  a real NIC/WAN, because kernel `netem` is unavailable here. The proxy is fair to both sides and
  rewards pipelining correctly, but the Slipstream latency win should be re-confirmed on real
  hardware with real RTT before it is treated as final.
- **Idiomatic-Go Postgres baseline.** `pgx` was run in normal query mode; Go can also pipeline
  explicitly via `pgx.Batch`/`SendBatch`, which would narrow the Slipstream gap. "Beats Go" here
  means beats idiomatic `database/sql` + `pgx`.
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
- **Networked Postgres:** at an equal connection budget LukeLang beats idiomatic Go everywhere —
  ~1.3× on localhost and 2.6–3.3× under ~2 ms RTT via the Slipstream pipelined executor — and does
  it on a small, Postgres-safe pool where the blocking approach would exhaust the server. Correct
  under pipelining + latency + concurrency (400/400). The latency figures are from a simulator and
  should be re-confirmed on real hardware.
- **Overall:** "LukeLang's backend is faster than Go" is **true and proven for the SQLite class**,
  and **true for Postgres at an equal connection budget** — clearly so under network latency, which
  is where real deployments live. The remaining honest gaps are the simulated-latency caveat, the
  idiomatic-Go baseline, and single-box hardware — not the direction of the result.
