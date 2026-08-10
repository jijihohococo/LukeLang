# LukeLang Strategy — Identity, Focus, and the Plan

> **Status:** v1 — decision record  
> **Purpose:** Lock down *what LukeLang is*, *what it wins first*, and *the order we build it in*.  
> When a roadmap or PR conflicts with this document, this document wins until it is explicitly revised.

---

## The identity (one sentence)

> **LukeLang is the conversational, reactive-native, full-stack language.**
> The **reactive engine is the moat**, the **conversational syntax is the face**, and the **browser is the renderer**.

Everything below follows from that sentence.

---

## Two kinds of "different"

LukeLang exists to be different from JS and Python. But there are two kinds of different, and only one of them helps us:

| Kind | Examples | Verdict |
| --- | --- | --- |
| **Difference the user feels and wants** | Conversational syntax; reactive-native model; (later) full-stack reactivity | **Our gold.** Users experience it and choose us for it. |
| **Difference under the floorboards that only costs us** | Hand-rolling layout math and paint the browser already does | **A tax.** Invisible to users, expensive for us, and always behind the browser. |

The mistake to avoid is putting our differentiation in the plumbing (invisible, expensive) instead of in the syntax and the reactive model (visible, loved). **Be different where the user stands, not under the floorboards.**

---

## The wedge: reactive full-stack web

General-purpose across web + scripting + backend + mobile + game means we are no one's first choice for anything. Every language that won did it by taking **one beachhead** and expanding from strength (JS→browser, Ruby→Rails, Go→servers, Rust→systems).

**Our beachhead is reactive full-stack web.** The thing almost nobody has cleanly is a **reactive cell that spans client and server**: backend data changes → the dependency graph knows exactly what depends on it → the precise frontend region repaints. No manual invalidation, no refetch plumbing. That is "LukeLang understands change" as something React-plus-a-REST-API cannot do — and it is the combination (conversational + reactive-native + full-stack) that no incumbent framework holds.

**Game dev and mobile are parked**, not cancelled — see [Parked tracks](#parked-tracks-earn-them-later).

---

## The renderer decision: compile to DOM + CSS (Path A)

There are exactly two coherent frontend strategies. The failure mode is doing neither and paying for both.

| Path | What it means | Cost / benefit |
| --- | --- | --- |
| **A — Compile to DOM + CSS** *(chosen)* | Let the browser lay out and paint. Argus stays a thin **reactive patcher** (dirty node → surgical DOM update); Hanka emits CSS flex/grid instead of computing absolute frames. | Small surface. Plays directly to the reactive moat (Solid/Svelte model). a11y, text, scroll, i18n, SEO come from the browser for free. |
| **B — Own the pixels (canvas/WebGL)** *(parked)* | Render to a canvas, touch the DOM **not at all** (the Flutter model). Owning layout+paint is only coherent here. | Flutter-sized effort. a11y/text/SEO from scratch. A real dream — but only earned later, and only in full form. |

**Today we were in the worst midpoint:** using the DOM as a host *and* bypassing its layout/paint. That collects Path B's costs and Path A's constraints. We choose **Path A**: the reactive graph driving surgical DOM updates *is* the product. The "own the pixels" dream moves to a parked track (see below).

---

## What we keep, unconditionally

- **Conversational syntax.** It is core identity and a felt differentiator. Keep it. The only action is to *stress-test its ceiling* (see Phase 3), not to soften it.
- **The reactive engine.** Our deepest, most defensible asset. Continue investing — but only *after* it has a public proof point (see Phase 2).

---

## Decision table

| # | Issue | Decision |
| --- | --- | --- |
| 1 | Positioning (5 domains) | Narrow to **one wedge: reactive full-stack web**. Game + mobile parked. |
| 2 | Conversational syntax | **Keep.** Stress-test at ~5k lines to learn its ceiling. |
| 3 | Argus / Hanka renderer | **Path A: compile to DOM + CSS.** Argus = reactive patcher; Hanka emits flex/grid. |
| 4 | "Own the pixels" dream | **Parked (Track 5, Flutter-style canvas).** Earned later, in full form only. |
| 5 | Frontend↔backend order | Stay frontend-first, but **spike a client↔server reactive cell now** to validate the true signature. |
| 6 | No proof point | **Build ONE reference app + publish a benchmark baseline** before any new engine phase. |

*(Hygiene items — CI, stale-object removal, legacy JS-emitter removal, `system()` checks — are already complete on `main`; see [`LEGACY.md`](./LEGACY.md) and `.github/workflows/ci.yml`.)*

---

## The plan (in build order)

### Phase 0 — Decide + clean *(done)*
- Identity locked (this document).
- Repo hygiene complete on `main`: CI runs the reactive conformance suite (`make test`); stale objects removed; legacy JS emitters removed; `system()` return values checked.

### Phase 1 — De-risk the two big bets *(done)*
- **Spike a client↔server reactive cell** (#5): **poll + push green** — poll: `fullstack_cell_{server,client}.luke`; push: `SUBSCRIBE` + SSE (`subscribe_cell_{server,client}.luke`) — server change → cell write → BIND → `THE REGION PAINT COUNT == 1` with **no fetch/timer/poll in user code** (`make test`).
- **Spike Path A** (#3): **POC green** — ROW/COLUMN emit CSS flex (`argus_flex` / `argus_flow_frame` / parent attach); `SIZE AUTO` → flex-grow on flow children; STACK/PLACE stay absolute; `reactive_greeting` still shows `region=1` on BIND. Full Hanka→CSS rewrite still open.

### Phase 2 — The proof point *(done — beachhead)*
- **Reference app (poll):** `examples/build/dashboard_{server,client}.luke` — live metrics, Path A flex, `region=1` per tick.
- **Flagship (push + fluid):** `examples/build/dashboard_push_{server,client}.luke` — SSE `SUBSCRIBE`, `SIZE AUTO` flex-grow, zero glue; `region=1` per push (`make test`).
- **Benchmark baseline:** `examples/build/reactive_benchmark.luke` + [`BENCHMARKS.md`](./BENCHMARKS.md) — granular vs full rebuild at 1K / 10K with **warmup + median/min** samples; `make test` asserts.
- **Mount path:** Argus id hash index (was O(N²)); arena grows by **block chain** (1 MiB start, no global 16 MiB bump); node ids owned/freed on CLEAR.
- Next ceilings: true integer type; backend concurrency (opens Backend track).

### Phase 3 — Expand from strength
- Resume reactive engine phases (roadmap in [`REACTIVE_ROADMAP.md`](./REACTIVE_ROADMAP.md)).
- Formalize the **syntax stress-test** (#2): hand-write one genuinely complex screen; compare readability against JSX at that size.
- Grow the frontend track ([`FRONTEND_ROADMAP.md`](./FRONTEND_ROADMAP.md)), then open the **Backend** track for full-stack reactivity.

---

## Parked tracks (earn them later)

These are **not cancelled** — they are sequenced after the beachhead is won, and are picked up from a position of strength (real users, a proven reactive core).

| Track | When | Note |
| --- | --- | --- |
| **Backend** | After the frontend proof point | Unlocks full-stack reactivity — likely the true signature. |
| **Scripting** | After backend | Tooling DX, packages, automation. |
| **Mobile** | Post-beachhead | Native shells / shared Luke UI. |
| **Own-the-pixels renderer (canvas/WebGL)** | Post-beachhead | The Flutter-style dream. Only in *full* form — never the DOM-host midpoint. |
| **Game** | Post-beachhead | Shares little code with reactive-DOM work; fights frame-budget/GPU constraints. Keep out of near-term PRs. |

---

## The wall sentence

> **Win reactive full-stack first. Keep the syntax. Let the browser render. Prove it with one app. Earn the rest later.**
