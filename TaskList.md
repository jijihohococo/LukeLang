# LukeLang Task List

Living checklist of structural gaps and next work. Status: `⬜` open · `🟡` partial · `✅` done · `⚫` parked.

---

## 🔴 1. Backend Framework — largest structural gap

Primitives (HTTP, concurrency, DB, Live Graph) exist; the app framework layer is still thin.

| Item | Status | Notes |
|------|--------|-------|
| Routing — route table, path params (`/user/:id`), method dispatch | ✅ | `ROUTES` + `HANDLE` + `SERVE ROUTES` codegen |
| Request parsing — query map, JSON body→value, form, headers, cookies | ✅ | query/header/cookie/JSON + `FORM` → `form_*_error` cells |
| Auth / session / login | 🟡 | Argon2id + session/CSRF + scoped `WATCH`; SECRET/FLOW/LIMIT/REVEAL — **no invented OAuth/TOTP** |
| **SQL injection** — parameterized bind for app `dbQuery` / `dbExec` | ✅ | `dbExecBind` / `dbQueryBind` (this ship); prefer over raw SQL in app code |
| `BACKEND_ROADMAP.md` track spine | ✅ | added (this ship) |
| Auth-as-types (unauthorized = compile error) | 🟡 | `SECRET` + scoped path; `FLOW` totality; `REVEAL` declassify — see [`docs/AUTH.md`](./docs/AUTH.md) |
| Migrations / schema helpers | ✅ | `SCHEMA`/`ENSURE SCHEMA`; `MIGRATION`/`MIGRATE`/`REWIND` |
| Declarative backend (routes/form/mw/schema) | ✅ | executable routes + form cells + migrate/rewind — see [`docs/BACKEND_ROADMAP.md`](./docs/BACKEND_ROADMAP.md) |
| C10K HTTP — pool, keep-alive, timeouts, graceful stop | ✅ | worker-side parse; chunked; SIGTERM; TLS via proxy — [`docs/DEPLOY.md`](./docs/DEPLOY.md) |

---

## 🟠 2. Live Graph — deepen

| Item | Status | Notes |
|------|--------|-------|
| True multi-join differential dataflow | ✅ | point keyed 2-table + N-table chains; **multi-row join bags** (`live_graph_join_multi` / `join_filter`); bag aggregates for equality, inequality/LIKE/IN, and **function predicates** (`live_graph_agg`, `agg_range`, `agg_fn`); subquery filters fall back to correct recompute |
| Server+client scrub by log seq | 🟡 | client buffer + scrub UI shipped; DevTools↔server log not fully wired |
| Free multicore parallelism from the graph | ⬜ | listed only |
| Wire hardening — backpressure, heartbeat/timeout, SSE channel auth | 🟡 | fail-closed SSE send; `LUKE_SSE_ORIGIN`; scoped PUSH requires user; `Last-Event-ID` resume |
| One deployed Luke app (wall sentence) | 🟡 | `examples/deploy/wall` + `scripts/wall_smoke.sh` + Caddyfile |

See [`docs/LIVE_GRAPH.md`](./docs/LIVE_GRAPH.md).

---

## 🟡 3. Renderer (Path A) — POC

| Item | Status | Notes |
|------|--------|-------|
| Full Hanka→CSS rewrite (default/complete) | ⬜ | flex emit proved; not default/complete |
| a11y — focus-trap / live regions | ⬜ | see ARGUS |
| Per-axis align | ⬜ | |
| Real responsive breakpoints | ⬜ | viewport helpers exist; richer breakpoints open |

See [`docs/STRATEGY.md`](./docs/STRATEGY.md), [`docs/FRONTEND_ROADMAP.md`](./docs/FRONTEND_ROADMAP.md).

---

## 🟡 4. Language fundamentals

| Item | Status | Notes |
|------|--------|-------|
| Replace line-based parser with real lexer/AST | ✅ | `parseLuke` Program/Stmt AST + Pratt exprs; BUILD/IR/FMT/LSP share IR — [`docs/AST.md`](./docs/AST.md) |
| Error UX / stack traces at scale | ⬜ | |
| `standard_library.md` drift (e.g. “File ops not yet implemented”) | ✅ | docs now match Build `std/files` |

See [`docs/BUILD_MODE.md`](./docs/BUILD_MODE.md).

---

## 🟡 5. Reactive polish

| Item | Status | Notes |
|------|--------|-------|
| Dead reaction elimination | ⬜ | |
| Parallel / race-safe cells | ⬜ | |
| Field-level tracking | ⬜ | |
| Time-travel deterministic replay spec | ⬜ | |
| Signature API / keyword polish | ⬜ | Reactive spec still v0.1 |

See [`docs/REACTIVE_ROADMAP.md`](./docs/REACTIVE_ROADMAP.md), [`docs/REACTIVE_SPEC.md`](./docs/REACTIVE_SPEC.md).

---

## ⚪ 6. Tooling & ecosystem (adoption blockers)

| Item | Status | Notes |
|------|--------|-------|
| LSP — completion / hover / go-to-def | ✅ | diagnostics + hover + completion + definition on Program AST |
| Debugger | ⬜ | |
| Formatter | 🟡 | `luke FMT` round-trip CI (`scripts/fmt_roundtrip_all.sh`): every `examples/build/*.luke` FMT→BUILD→identical stdout |
| Real remote package registry (versions / signing) | 🟡 | local index + `sha256` verify on install; signing still open |
| Third-party libs (auth, DB drivers, cloud SDKs) | ⬜ | ~1–2 packages |
| lukelang.org / docs site | 🟡 | `site/index.html` stub |

---

## ⚫ 7. Parked (correctly deferred)

| Track | Status | Notes |
|-------|--------|-------|
| Scripting | ⚫ | after backend |
| Mobile | ⚫ | post-beachhead |
| Game | ⚫ | post-beachhead |
| Own-the-pixels renderer (canvas/WebGL) | ⚫ | full form only — never DOM-host midpoint |

---

## Active focus

**Now:** Program AST is the compile front-end ([`docs/AST.md`](./docs/AST.md)). Execution is **A+** on the Live Graph differential ledger ([`docs/SCORECARD.md`](./docs/SCORECARD.md), [`docs/LIVE_GRAPH.md`](./docs/LIVE_GRAPH.md)). Wall stays the product proof. Mobile/game stay parked.
