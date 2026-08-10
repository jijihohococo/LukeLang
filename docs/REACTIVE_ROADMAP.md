# LukeLang Reactive — Production Roadmap

> Phases **1–8** shipped (feature foundation).  
> This roadmap covers **production-grade + signature-grade** work.  
> **Priority:** Correctness → Scheduler → Granularity → Memory → DevTools → Compiler → Concurrency → Benchmarks → Spec polish.

Normative semantics: [`REACTIVE_SPEC.md`](./REACTIVE_SPEC.md)

---

## Track status

| # | Area | Phase 9+ focus | Status |
|---|------|----------------|--------|
| 1 | **Reactive correctness** | Stale dep cleanup, deterministic order, cycle spec, nested rules | 🟡 v0.1 |
| 2 | **Scheduler 2.0** | Priority, dedup, micro/macrotask queues, starvation | 🟡 v0.2 |
| 3 | **Granularity** | Field-level, Hanka regions, Argus paint regions | 🟡 v0.3 |
| 4 | **Memory management** | Auto cleanup, dead nodes, weak refs, leak detect | ✅ v0.4 |
| 5 | **Error system** | Isolation, async failure, boundaries, retry | ⬜ |
| 6 | **DevTools** | Live graph, why changed/repaint, timelines | 🟡 v0.5 started |
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

## Milestone A — Correctness foundation

**Done when:**

- [x] `REACTIVE_SPEC.md` v0.1
- [x] Stale dependency cleanup (`luke_rx_clear_deps`)
- [x] Scheduler counters + Build introspection
- [x] `reactive_conformance_*.luke` + `make test-build`
- [ ] Nested/recursive reaction rules documented + tested
- [ ] Error propagation policy (v0.2)

---

## Milestone B — Scheduler 2.0

- [x] Priority lanes (UI effect before BACKGROUND)
- [x] Dirty dedup counter + queue size instrumentation
- [x] Deferred nested flush (one turn, multiple passes)
- [x] Scheduler timeline / step counters
- [x] `WHEN REACTIVE` / `WHEN BACKGROUND REACTIVE` side-effect effects
- [x] `BIND BACKGROUND` low-priority binds
- [ ] Starvation guard (wait_epochs boost) — runtime hook present, conformance pending
- [ ] Macrotask queue / `scheduleReactive` API

---

## Milestone C — Granularity

- [x] Argus region paint (`argus_paint_one`, `THE REGION PAINT COUNT`)
- [x] Hanka partial relayout (`hanka_layout_dirty`, `hanka_mark_region`)
- [x] Component subtree invalidation on scope dispose
- [x] `reactive_conformance_subtree.luke`
- [ ] Field-level object tracking
- [ ] Paint/layout dirty rects (spatial index)

---

## Milestone E — Memory management *(shipped v0.4)*

- [x] Dead node disposal + `THE DISPOSED COUNT`
- [x] Graph audit + leak edge repair (`luke_rx_audit_graph`, `THE LEAK EDGE COUNT`)
- [x] Alive/dead counters (`THE ALIVE NODE COUNT`, `THE DEAD NODE COUNT`)
- [x] Weak reads (`THE WEAK VALUE OF`, `THE WEAK READ COUNT`)
- [x] `AUDIT REACTIVE` statement
- [x] Conformance: `reactive_conformance_{memory,weak}.luke`
- [x] Weak effect refs (`WHEN REACTIVE WEAK`, `luke_rx_effect_weak`)
- [x] Auto scope GC (`luke_rx_scope_gc`, `UNMOUNT COMPONENT`, `THE SCOPE GC COUNT`)
- [x] Conformance: `reactive_conformance_{scope_gc,weak_effect}.luke`

---

## Milestone F — DevTools *(current)*

- [x] Graph stats (`THE GRAPH CELL COUNT`, `THE GRAPH EDGE COUNT`)
- [x] Why-changed trace (`THE WHY ROOT OF`, `THE WHY DEPTH OF`, `TRACE WHY`)
- [x] Last write id + dep/sub counts per node
- [x] Timeline step export (`THE TIMELINE STEP ID AT n`)
- [x] `DUMP REACTIVE GRAPH` live snapshot (stderr)
- [x] Conformance: `reactive_conformance_devtools.luke`

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
