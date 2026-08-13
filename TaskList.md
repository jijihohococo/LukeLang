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
| **SQL injection** — parameterized bind for app `dbQuery` / `dbExec` | ✅ | `dbExecBind` / `dbQueryBind`; TLS pool + stmt cache (no open/prepare per request) |
| `BACKEND_ROADMAP.md` track spine | ✅ | added (this ship) |
| Auth-as-types (unauthorized = compile error) | 🟡 | `SECRET` + scoped path; `FLOW` totality; `REVEAL` declassify — see [`docs/AUTH.md`](./docs/AUTH.md) |
| Migrations / schema helpers | ✅ | `SCHEMA`/`ENSURE SCHEMA`; `MIGRATION`/`MIGRATE`/`REWIND` |
| Declarative backend (routes/form/mw/schema) | ✅ | executable routes + form cells + migrate/rewind — see [`docs/BACKEND_ROADMAP.md`](./docs/BACKEND_ROADMAP.md) |
| C10K HTTP — event-loop I/O + handler pool | ✅ | SO_REUSEPORT multi-loop; arena/job pools; TCP_NODELAY; writev; SIGTERM; TLS via proxy — [`docs/DEPLOY.md`](./docs/DEPLOY.md) |

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
| Full Hanka→CSS (flex default; STACK/PLACE absolute; **GRID** opt-in) | ✅ | ROW/COLUMN → flex; `BEGIN GRID … COLUMNS n`; STACK/PLACE absolute |
| Per-axis align | ✅ | `ALIGN MAIN … CROSS …` · `ALIGN main, cross` |
| Scroll container | ✅ | `BEGIN … SCROLL` → `overflow:auto` |
| a11y — focus-trap / live regions | ✅ | modal via `OPEN/CLOSE THE MODAL`; `SLOT … ANNOUNCE`; `TRAP FOCUS` |
| Real responsive breakpoints | ✅ | `STACK BELOW` / `WRAP BELOW`; `WHEN … BELOW/ABOVE`; matchMedia `AT LEAST/UNDER/BETWEEN` |
| Tailwind interop (class hatch) | ✅ | `WEAR "classes"` on SLOT/BEGIN; `PUBLISH WEB --tailwind input.css` |

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

Core editor protocol surface is in place (LSP + DAP + FMT + `#line` maps). **Distribution**
work (registry, signing, packages, VS Code client, docs site) is **parked** — see §7.

| Item | Status | Notes |
|------|--------|-------|
| LSP — completion / hover / go-to-def | ✅ | rich hover (types/signatures/docs); documentSymbol/refs/rename/signatureHelp/formatting/semanticTokens/codeAction; context-aware completion; didChange re-diagnose CI |
| Debugger | 🟡 | `#line` + `luke DEBUG` break/step/inspect (cells + deps); `luke DAP` stdio adapter (gdb backend) |
| Formatter | 🟡 | `luke FMT` round-trip CI (`scripts/fmt_roundtrip_all.sh`): every `examples/build/*.luke` FMT→BUILD→identical stdout |

---

## ⚫ 7. Parked (correctly deferred)

| Track | Status | Notes |
|-------|--------|-------|
| Scripting | ⚫ | after backend |
| Mobile | ⚫ | post-beachhead |
| Game | ⚫ | post-beachhead |
| Own-the-pixels renderer (canvas/WebGL) | ⚫ | full form only — never DOM-host midpoint |

### Distribution (parked — not on the active tooling path)

Parked until the wall / Live Graph product sentence is the focus of adoption energy.
Do not un-park to chase a Tooling A+ checklist.

| # | Item | Status | Notes |
|---|------|--------|-------|
| 12 | Package signing | ⚫ | Registry has `sha256` verify on install; **signed publish + verify** still open — park for distribution |
| 13 | Real remote registry | ⚫ | Today: local `luke_modules` index. Hosted, versioned registry + **semver resolution** — park for distribution |
| 14 | Third-party libraries | ⚫ | ~1–2 toy packages. Real libs (HTTP client, DB driver wrapper, …) — park for ecosystem / distribution |
| 15 | Editor extensions | ⚫ | VS Code (or JetBrains) client: LSP + DAP + syntax highlighting. Protocol servers exist; **no published client** — park for distribution |
| 16 | lukelang.org / docs site | ⚫ | `site/index.html` stub only. Real docs site — park for distribution |

---

## Active focus

**Frontend track is done** ([`docs/FRONTEND_ROADMAP.md`](./docs/FRONTEND_ROADMAP.md)). **Now:** deepen Backend / Live Graph beachhead ([`docs/BACKEND_ROADMAP.md`](./docs/BACKEND_ROADMAP.md), [`docs/LIVE_GRAPH.md`](./docs/LIVE_GRAPH.md)). Wall stays the product proof. Mobile/game stay parked. **Distribution** stays parked — do not dilute Ambition by un-parking it for a tooling checklist.
