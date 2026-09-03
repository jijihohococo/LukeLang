# LEFT_TO_COOK.md — Production guarantee checklist

> **Purpose:** Everything in this file must be eliminated (`[x]`) before we claim:
> **“LukeLang is ready for production use at the level of Go / Python / Rust for real backends — including microservices and other common architectures, not monolith-only.”**
>
> **Rule:** If an item is still `[ ]`, we do **not** make that production claim. Thesis ≠ production.
>
> **How we work:** Pick one open item. Finish it. Check it off. Commit. Next. No skipping gates that say *BLOCKER*.

Status legend: `[ ]` open · `[~]` partial / in progress · `[x]` done (proven with test, doc, or live deploy)

Related docs (do not replace this file): [`docs/STRATEGY.md`](./docs/STRATEGY.md) · [`TaskList.md`](./TaskList.md) · [`docs/BACKEND_ROADMAP.md`](./docs/BACKEND_ROADMAP.md) · [`docs/DEPLOY.md`](./docs/DEPLOY.md) · [`docs/LIVE_GRAPH.md`](./docs/LIVE_GRAPH.md)

---

## 0. Definition of Done (the guarantee)

When **every** section below is fully checked, LukeLang has earned this statement:

1. A mid-level engineer who knows Go or Python can ship a **production HTTP service** in Luke in **one day** (init → test → container → metrics → on-call basics).
2. That service can sit in a **microservice topology** (HTTP/gRPC neighbor, not “owns the whole company graph”).
3. Failures are **observable, recoverable, and documented** (panic, DB down, dependency timeout, deploy rollback).
4. There is **at least one real production system** running Luke under load with public uptime history.
5. Language + stdlib + toolchain are **versioned and stable** enough that a company can pin and sleep.

Until then: learning / beachhead / thesis — **not** “use this instead of Go in prod.”

---

## 1. One real production system *(BLOCKER)*

Without this, nothing else counts.

- [ ] **P0 — Wall app in production:** One Luke service with real users or real internal traffic (not only `examples/`). Candidate: lukelang.org ops / packages / auth / learn — or a dedicated public reference app.
- [ ] **P0 — Public uptime:** Status page with ≥ 30 days history (and a path to 90 days).
- [ ] **P0 — Incident log:** At least one written postmortem template + one real or game-day incident recorded.
- [ ] **P0 — Rollback drill:** Documented + practiced: previous binary/release restored in &lt; 15 minutes.
- [ ] **P0 — Load evidence:** Sustained load test numbers published (RPS, p50/p95 latency, error rate) for the wall app — not only microbenchmarks.

---

## 2. Golden path: stranger → production in one day *(BLOCKER)*

- [ ] **G0 — `mimo init --template api`:** Ships health, routes, SQLite/Postgres choice, tests stub. *(partial today)*
- [ ] **G0 — `mimo init --template service`:** Stateless HTTP worker meant for k8s/microservice (no Live Graph required).
- [ ] **G1 — Official packages:** `router`, `validate`, `migrate`, `health`, `logger`, `config` on packages.lukelang.org — versioned, documented, with examples.
- [ ] **G2 — Docker:** Official Dockerfile + multi-arch image story for a Luke API.
- [ ] **G2 — Compose:** `docker compose` reference (app + Postgres + reverse proxy).
- [ ] **G3 — systemd unit:** Production unit file + env file pattern (no broken `Environment=` splitting).
- [ ] **G3 — Kubernetes:** Deployment + Service + probes (liveness/readiness) + ConfigMap/Secret examples.
- [ ] **G4 — `mimo deploy` or 10-minute doc:** One canonical “ship this” path; CI checks the doc still works.
- [ ] **G5 — Day-1 tutorial on lukelang.org/learn:** Clone → run → test → container → hit `/health` in prod-like env.

---

## 3. Language & toolchain maturity (Go/Rust/Python bar)

### 3.1 Stability contract

- [ ] **L0 — Semver policy:** Language, stdlib, and `mimo` channels documented (what can break, what cannot).
- [ ] **L0 — Compatibility tests:** CI suite that fails if a pinned minor release breaks published examples.
- [ ] **L1 — `luke.json` pins:** toolchain version + dependency lock (`luke.lock`) required for release builds.
- [ ] **L1 — Reproducible builds:** Same source + lock → bit-stable or ABI-stable artifacts (document what “stable” means).

### 3.2 Compiler / runtime correctness

- [ ] **L2 — AST-driven BUILD:** Codegen consumes AST directly; no flatten-to-v1-text on the production path. See [`docs/AST.md`](./docs/AST.md).
- [ ] **L2 — Syntax v2 complete:** Spec + corpus + no v1 required for new code. See [`docs/SYNTAX_V2_SPEC.md`](./docs/SYNTAX_V2_SPEC.md).
- [ ] **L3 — Panic / abort policy:** Documented: what traps, what returns error, what kills the process; recovery hooks for HTTP workers.
- [ ] **L3 — Memory model doc:** Arena lifetimes, what is safe across requests/threads, what is not — written for systems programmers.
- [ ] **L3 — Undefined behavior audit:** Checklist + CI greps / sanitizer builds (ASan/UBSan) on core runtime.
- [ ] **L4 — Deterministic error messages:** Stable error codes + file:line; stack traces usable in production logs.

### 3.3 Developer tooling

- [ ] **T0 — LSP:** Diagnostics + go-to-def + completion good enough for daily editing (not beachhead-only).
- [ ] **T0 — Formatter:** `luke FMT` idempotent; CI format check.
- [ ] **T1 — Debugger:** `luke DAP` / DEBUG reliable on Linux + macOS for server code.
- [ ] **T1 — Test runner:** First-class `mimo test` / `luke TEST` with parallel runs, fixtures, coverage report.
- [ ] **T2 — GitHub Linguist:** LukeLang registered so `.luke`/`.lk` appear as the language (grammar + samples + upstream PR).
- [ ] **T2 — Editor installs:** VS Code / Open VSX extension published and version-matched to toolchain.

---

## 4. Stdlib & platform (parity surface)

Ship what production backends actually import. Prefer **thin, boring, correct** over clever.

### 4.1 HTTP & networking

- [ ] **N0 — HTTP client:** Timeouts, redirects policy, connection reuse, TLS verify — first-class (`std/http` client).
- [ ] **N0 — HTTP/2 or clear non-support:** Either implement or document “HTTP/1.1 + proxy terminates H2” as the supported prod path.
- [ ] **N1 — Outbound retries / backoff helpers:** Idempotent retry policy helpers (not magic).
- [ ] **N1 — gRPC *or* official “use Envoy/JSON-HTTP” stance:** Pick one and document; if no gRPC, provide a blessed interop pattern.
- [ ] **N2 — WebSocket (optional but common):** Or explicit “not supported; use SSE/HTTP.”
- [ ] **N2 — DNS / dial errors:** Typed, inspectable network errors.

### 4.2 Data

- [ ] **D0 — Postgres production defaults:** Pool limits, timeouts, cancel, migration story — battle-tested docs.
- [ ] **D0 — SQLite production guidance:** When yes / when no (multi-writer, NFS, etc.).
- [ ] **D1 — Redis or equivalent cache client:** Or documented external-cache pattern via HTTP/TCP.
- [ ] **D1 — Migrations:** Versioned, forward/back, CI-applied; already started — finish + package.
- [ ] **D2 — Queues:** At least one blessed path (Postgres skip-locked / Redis / NATS / SQS via SDK) with example workers.

### 4.3 Encoding, time, crypto, files

- [ ] **E0 — JSON:** Encode/decode completeness + streaming for large payloads.
- [ ] **E0 — Time/duration/TZ:** Explicit, tested; no “stringly” time in stdlib examples.
- [ ] **E1 — Crypto:** Only via vetted libs (libsodium already); high-level helpers documented.
- [ ] **E1 — Files / temp / paths:** Safe path join; no traversal footguns in examples.
- [ ] **E2 — Compression / multipart / URL:** Common request shapes covered.

### 4.4 Concurrency

- [ ] **C0 — Concurrency model doc:** Threads, pools, `httpServe` workers, what shares memory, what must not.
- [ ] **C0 — Race detection story:** Tooling or documented discipline + tests.
- [ ] **C1 — Structured timeouts:** Deadline propagation across client calls and DB.
- [ ] **C1 — Graceful shutdown:** Already partial — extend to drain in-flight + queue workers + k8s preStop.

---

## 5. Microservices & multi-architecture *(BLOCKER for “not monolith-only”)*

**Hard rule:** Live Graph is for **Luke-owned data planes inside a trust boundary**. It is **not** the company-wide distributed database. CAP still applies. Cross-service change uses **explicit contracts**.

### 5.1 Service-as-citizen

- [ ] **M0 — Stateless service template:** No shared local SQLite; config via env; `/healthz` + `/readyz`.
- [ ] **M0 — 12-factor checklist:** Doc + CI example proving compliance for the template.
- [ ] **M1 — Service identity:** Example of service-to-service auth (mTLS via mesh/proxy **or** signed tokens) — document the blessed path.
- [ ] **M1 — API contracts:** OpenAPI or protobuf export/import story for Luke HTTP APIs.
- [ ] **M2 — Sidecar / mesh friendly:** Runs behind Envoy/nginx/Caddy without custom kernel networking.

### 5.2 Distributed realities (honest mechanisms)

- [ ] **M3 — Timeout + bulkhead patterns:** Per-dependency budgets; isolate thread/pool pools per dependency.
- [ ] **M3 — Circuit breaker / shed load:** Library or std pattern with tests.
- [ ] **M3 — Idempotency keys:** Helpers for safe retries on writes.
- [ ] **M4 — Outbox / inbox example:** Reliable event publish between services (Postgres outbox → bus).
- [ ] **M4 — Consumer workers:** At-least-once handler example with dedupe.
- [ ] **M5 — Distributed tracing:** W3C traceparent propagation; export to OTLP/Jaeger/Zipkin (or OpenTelemetry).
- [ ] **M5 — Correlation IDs:** Request ID in logs/metrics across service hops.
- [ ] **M6 — Multi-instance deploy:** Wall app scaled to N replicas behind LB; sticky sessions **not** required for the service template.
- [ ] **M6 — CAP doc:** Explicit: what Luke guarantees single-node vs multi-node; no marketing that erases partitions.

### 5.3 Live Graph boundaries

- [ ] **LG0 — Boundary doc:** “Use Live Graph inside a service; use events/HTTP between services.”
- [ ] **LG1 — Multi-tenant isolation tests:** `FOR CURRENT USER` / tenant scoping under concurrent load.
- [ ] **LG2 — Failure modes:** SSE drop, DB blip, reconnect, backlog — tested + runbook.
- [ ] **LG3 — Optional multi-node later:** Only after M0–M6; separate RFC — not a silent assumption.

---

## 6. Observability, security, ops *(BLOCKER)*

### 6.1 Observability

- [ ] **O0 — Structured logging:** JSON logs; levels; no silent swallow.
- [ ] **O0 — Metrics:** RED/USE basics (requests, errors, duration, saturation) — Prometheus or OTEL meters.
- [ ] **O1 — Profiling hooks:** CPU/heap profile recipe for native binaries.
- [ ] **O1 — Continuous profiling optional:** Documented path.

### 6.2 Security

- [ ] **S0 — Threat model:** Short doc for HTTP API + Live Graph + auth.
- [ ] **S0 — Secrets:** Env/file only; no secrets in images; rotation recipe.
- [ ] **S1 — Dependency supply chain:** Checksums for `mimo forge` (started) + SBOM or provenance for releases.
- [ ] **S1 — Security advisory process:** How to report/fix CVEs.
- [ ] **S2 — TLS:** Prod path via reverse proxy verified; `LUKE_TRUST_PROXY` hardened defaults.
- [ ] **S2 — Auth packages:** Session/OAuth/TOTP interop finished to “copy this for prod” quality.

### 6.3 Ops

- [ ] **OP0 — Runbooks:** Deploy, rollback, DB migrate, certificate, disk full, thundering herd.
- [ ] **OP0 — Backup/restore:** For Postgres-backed reference app.
- [ ] **OP1 — Resource limits:** Memory/CPU guidance; arena sizing; FD limits.
- [ ] **OP1 — Chaos/game day:** Kill DB, kill dependency, kill pod — expected behavior matches docs.

---

## 7. Ecosystem & adoption

- [ ] **A0 — Package registry quality:** Search, versions, yank, docs pages; signing roadmap.
- [ ] **A1 — Three production recipes:** (1) public API, (2) authenticated app, (3) worker + queue — each with tests.
- [ ] **A2 — Interop cookbooks:** Call Luke from Go/Python; call Go/Python from Luke (FFI/HTTP).
- [ ] **A3 — Hiring doc:** “What a Luke engineer knows”; interview exercises.
- [ ] **A4 — Public benchmarks vs Go/Python** for HTTP + DB — honest methodology, reproducible scripts.
- [ ] **A5 — Community:** Contribution guide, CODEOWNERS, issue templates, security policy.

---

## 8. Architecture matrix (must not be monolith-only)

For each row: **example repo or doc + CI green + one deploy**.

| Architecture | Proof required | Done |
|---|---|---|
| Single binary monolith (API + worker optional) | Reference app | [ ] |
| Modular monolith (packages/boundaries inside one process) | Doc + example layout | [ ] |
| Microservices (2+ Luke services + one non-Luke neighbor) | Compose/k8s demo | [ ] |
| Luke worker + external DB + managed queue | Example | [ ] |
| Edge/proxy TLS termination | Deploy doc | [ ] |
| Multi-region *active-passive* (failover) | Runbook + drill | [ ] |
| Multi-region *active-active* | Explicitly out of scope until CAP RFC | [ ] N/A or later |

---

## 9. Acceptance gates (do not reorder)

Check these only when the sections above they depend on are done.

- [ ] **GATE A — Golden path:** New contributor ships `service` template to a container on a VM/k8s in &lt; 1 day without asking chat.
- [ ] **GATE B — Microservice:** Two Luke services + one Go/Python stub exchange authenticated HTTP with timeouts, retries, and traces.
- [ ] **GATE C — Production wall:** Real app, 30-day uptime, load numbers, rollback drill.
- [ ] **GATE D — Toolchain:** LSP + FMT + TEST + pinned releases used by the wall app.
- [ ] **GATE E — Public claim:** README / lukelang.org may say production-ready **only after A–D**.

**Final checkbox:**

- [ ] **PRODUCTION GUARANTEE:** Gates A–E are `[x]`. LukeLang may be recommended for production backends alongside Go/Python/Rust within the documented architecture matrix.

---

## 10. Working rules (how we eliminate)

1. **One item per PR** when possible; reference the checkbox ID (`P0`, `M3`, …) in the PR body.
2. **No checkbox without evidence:** link to commit, test name, URL, or doc section.
3. **Prefer delete scope over add fantasy:** if an item is wrong, replace it with a sharper item — do not silently ignore.
4. **Live Graph does not excuse ops:** a beautiful cell graph with no metrics still fails GATE C.
5. **Critics were right about risk:** we answer with artifacts, not adjectives.

---

## 11. Immediate next three (start here)

Update this list every time the top is cleared.

1. [ ] **P0 — Wall app in production** (pick target, ship Luke backend to lukelang.org or separate host)
2. [ ] **G1 — Five packages** (`health`, `logger`, `config`, `validate`, `migrate`) published and used by the wall app
3. [ ] **M0 — Stateless `service` template** + Compose with Postgres + proxy (proves not monolith-only)

---

*When this file is all `[x]` under sections 1–9 and the Final checkbox is ticked, the thesis has become a production language. Until then: cook.*
