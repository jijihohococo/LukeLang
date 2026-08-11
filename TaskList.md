# LukeLang Task List

Living checklist of structural gaps and next work. Status: `⬜` open · `🟡` partial · `✅` done · `⚫` parked.

---

## 🔴 1. Backend Framework — largest structural gap

Primitives (HTTP, concurrency, DB, Live Graph) exist; the app framework layer is still thin.

| Item | Status | Notes |
|------|--------|-------|
| Routing — route table, path params (`/user/:id`), method dispatch | 🟡 | `httpMatch` beachhead (this ship); declarative route table still open |
| Request parsing — query map, JSON body→value, form, headers, cookies | 🟡 | query map + header/cookie + JSON-via-body (this ship); form still open |
| Auth / session / login | 🟡 | Argon2id (libsodium) + secure session cookie + CSRF + `REQUIRE LOGIN` / `THE CURRENT USER` / `WATCH … FOR CURRENT USER` beachhead; 2FA/OAuth/reset still open |
| **SQL injection** — parameterized bind for app `dbQuery` / `dbExec` | ✅ | `dbExecBind` / `dbQueryBind` (this ship); prefer over raw SQL in app code |
| `BACKEND_ROADMAP.md` track spine | ✅ | added (this ship) |

---

## 🟠 2. Live Graph — deepen (prototype)

| Item | Status | Notes |
|------|--------|-------|
| True multi-join differential dataflow | ⬜ | today: recompute-on-both-tables |
| Server+client scrub by log seq | 🟡 | client buffer + scrub UI shipped; DevTools↔server log not fully wired |
| Free multicore parallelism from the graph | ⬜ | listed only |
| Wire hardening — backpressure, heartbeat/timeout, SSE channel auth | 🟡 | `Last-Event-ID` resume shipped |

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
| Replace line-based parser with real lexer/AST | ⬜ | parenthesis / nested expr ceiling (e.g. `(t1 SUBTRACT t0)`) |
| Error UX / stack traces at scale | ⬜ | |
| `standard_library.md` drift (e.g. “File ops not yet implemented”) | ⬜ | Build `std/files` exists — doc must catch up |

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
| LSP — completion / hover / go-to-def | 🟡 | `luke LSP` diagnostics beachhead only |
| Debugger | ⬜ | |
| Formatter | ⬜ | |
| Real remote package registry (versions / signing) | ⬜ | local `luke_modules` only |
| Third-party libs (auth, DB drivers, cloud SDKs) | ⬜ | ~1–2 packages |
| lukelang.org / docs site | ⬜ | confirm / ship |

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

**Now:** Backend Framework (section 1) — security bind first, then routing / parsing / session beachhead + roadmap spine.
