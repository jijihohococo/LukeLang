# LukeLang Reactive — Production Roadmap

> Phases **1–8** shipped (feature foundation).  
> This roadmap covers **production-grade + signature-grade** work.  
> **Priority:** Correctness → Scheduler → Granularity → Memory → DevTools → Compiler → Concurrency → Benchmarks → Spec polish.

Normative semantics: [`REACTIVE_SPEC.md`](./REACTIVE_SPEC.md)

---

## Track status

| # | Area | Phase 9+ focus | Status |
|---|------|----------------|--------|
| 1 | **Reactive correctness** | Stale dep cleanup, deterministic order, cycle spec, nested rules | 🟡 v0.1 started |
| 2 | **Scheduler 2.0** | Priority, dedup, micro/macrotask queues, starvation | ⬜ |
| 3 | **Granularity** | Field-level, Hanka regions, Argus paint regions | ⬜ |
| 4 | **Memory management** | Auto cleanup, dead nodes, weak refs, leak detect | ⬜ |
| 5 | **Error system** | Isolation, async failure, boundaries, retry | ⬜ |
| 6 | **DevTools** | Live graph, why changed/repaint, timelines | ⬜ |
| 7 | **Time-travel** | Snapshots, replay, deterministic replay | ⬜ |
| 8 | **Compiler optimization** | Static graph, dead reaction elimination | ⬜ |
| 9 | **Concurrency** | Parallel reactions, workers, race-safe cells | ⬜ |
| 10 | **Persistence** | Persistent cells, cache invalidation, offline sync | 🟡 QUERY beachhead |
| 11 | **Cross-runtime consistency** | Native/WASM/backend/game same semantics tests | 🟡 conformance harness |
| 12 | **Benchmark suite** | 1K/10K/100K nodes, workloads | ⬜ |
| 13 | **Reactive specification** | Official semantics doc | 🟡 v0.1 |
| 14 | **Signature API polish** | Conversational idioms, keyword trim | ⬜ |
| 15 | **Real-world stress apps** | Todo, dashboard, chat, game, full-stack | ⬜ |

---

## Milestone A — Correctness foundation *(current)*

**Done when:**

- [x] `REACTIVE_SPEC.md` v0.1
- [x] Stale dependency cleanup (`luke_rx_clear_deps`)
- [x] Scheduler counters + Build introspection
- [x] `reactive_conformance_*.luke` + `make test-build`
- [ ] Nested/recursive reaction rules documented + tested
- [ ] Error propagation policy (v0.2)

---

## Milestone B — Scheduler 2.0

- Priority lanes (UI effect > derived > background)
- Dirty dedup queue
- Starvation guard + scheduler timeline export

---

## Milestone C — Granularity

- Component subtree invalidation
- Hanka layout-region dirty rects
- Argus paint-region (not whole-node default)

---

## Milestone D — Production proof

- Benchmark suite vs full rerender baseline
- One **signature reference app** (todo or dashboard)
- WASM/native conformance CI matrix

---

## Definition of done: “fundamental execution model”

Reactive is not a “feature” when all of:

1. Normative spec with guarantees  
2. Conformance tests (native + WASM)  
3. Correctness + disposal + async failure suite  
4. Reference stress app with measurable granular updates  
5. Benchmark baselines published  
6. Signature idioms documented (“Lukelang way”)

---

## Signature line

> **Lukelang understands change.**

The runtime knows **what depends on what** — and only that work runs.
