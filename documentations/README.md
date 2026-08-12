# LukeLang — Documentations

Project documentation hub. Canonical long-form docs live under [`../docs/`](../docs/). Technical papers live under [`papers/`](./papers/README.md).

## Technical papers

| Paper | Subject |
|-------|---------|
| [01 Reactive Engine](./papers/01-reactive-engine.md) | Cells, dependency graph, scheduler, memory, tooling, perf |
| [02 Frontend: Argus & Hanka](./papers/02-frontend-argus-hanka.md) | Layout + surgical paint, Path A |
| [03 Live Graph](./papers/03-live-graph.md) | DB row → pixel, IVM, SSE, time-travel |
| [04 Auth: Secure by Compiler](./papers/04-auth.md) | SECRET / FLOW / LIMIT / REVEAL |
| [05 Request Pipeline / Middleware](./papers/05-middleware.md) | ROUTES, capabilities, FORM/SCHEMA compile gates |
| [06 OAuth & Auth Flows](./papers/06-oauth.md) | VERIFY BY OAUTH / FLOW state machines |
| [07 Execution](./papers/07-execution.md) | Build pipeline (native/WASM/browser), arena, differential ledger |
| [08 Architecture](./papers/08-architecture.md) | Shared program tree; row → pixel layering |
| [09 Core Engines](./papers/09-core-engines.md) | Arena/text/index/cell/trigger substrate; engine composition |

Index: [`papers/README.md`](./papers/README.md).

## Start here

| Doc | What |
|-----|------|
| [Strategy](../docs/STRATEGY.md) | Identity, wedge, plan |
| [Getting Started](../docs/getting_started.md) | First program |
| [Task List](../TaskList.md) | Living checklist |

## Language & Build

| Doc | What |
|-----|------|
| [Language Reference](../docs/language_reference.md) | Syntax surface |
| [Standard Library](../docs/standard_library.md) | `std/*` inventory |
| [Build Mode](../docs/BUILD_MODE.md) | AOT / native / browser |
| [INTEGER](../docs/INTEGER.md) | Exact int64 rules |
| [Advanced Topics](../docs/advanced_topics.md) | Deeper language notes |

## Auth (secure by compiler)

| Doc | What |
|-----|------|
| [Auth](../docs/AUTH.md) | SECRET / FLOW / LIMIT / REVEAL / audit |
| [Backend Roadmap](../docs/BACKEND_ROADMAP.md) | HTTP / session / framework gaps |
| [Paper 04 — Auth](./papers/04-auth.md) | Long-form secure-by-compiler |

## Reactive & Live Graph

| Doc | What |
|-----|------|
| [Live Graph](../docs/LIVE_GRAPH.md) | DB row → pixel |
| [Reactive](../docs/REACTIVE.md) | Client reactive engine |
| [Reactive Spec](../docs/REACTIVE_SPEC.md) | Scheduler contract |
| [Reactive Roadmap](../docs/REACTIVE_ROADMAP.md) | Production milestones |
| [Paper 01 — Reactive](./papers/01-reactive-engine.md) | Long-form engine paper |
| [Paper 03 — Live Graph](./papers/03-live-graph.md) | Long-form Live Graph paper |

## Frontend / render

| Doc | What |
|-----|------|
| [Frontend Roadmap](../docs/FRONTEND_ROADMAP.md) | Argus / Hanka / publish |
| [Argus](../docs/ARGUS.md) | Rendering (DOM presentment) |
| [Hanka](../docs/HANKA.md) | Layout engine |
| [Production Web](../docs/PRODUCTION_WEB.md) | Forms, routes, deploy |
| [Rendering Engine](../docs/RENDERING_ENGINE.md) | Engine notes |
| [Layout Engine](../docs/LAYOUT_ENGINE.md) | Layout notes |
| [Paper 02 — Frontend](./papers/02-frontend-argus-hanka.md) | Long-form Argus/Hanka paper |

## Other

| Doc | What |
|-----|------|
| [Benchmarks](../docs/BENCHMARKS.md) | Perf notes |
| [Contributor Guide](../docs/contributor_guide.md) | How to contribute |
| [Legacy](../docs/LEGACY.md) | Removed JS emitters |
| [Docs index](../docs/README.md) | Full `docs/` table of contents |

Full index: [`docs/README.md`](../docs/README.md).
