# LukeLang Reactive Specification v0.1

> **Status:** Normative draft (Phase 9–10 — correctness + Scheduler 2.0)  
> **Implementation:** `vm/runtime/luke_reactive.h`  
> **Architecture overview:** [`REACTIVE.md`](./REACTIVE.md)  
> **Roadmap:** [`REACTIVE_ROADMAP.md`](./REACTIVE_ROADMAP.md)

This document defines **scheduler guarantees** for the Luke Reactive Runtime.  
Surface syntax may evolve; these semantics are the contract for native and WASM.

---

## 1. Graph model

### 1.1 Node kinds

| Kind | Role | Writes | Reads trigger |
|------|------|--------|----------------|
| `CELL` | Source value (number/text) | `CHANGE`, `INCREASE`, writers | `luke_rx_read_*` |
| `DERIVED` | Pure compute | Never directly | compute fn reads |
| `EFFECT` | Side effect | May touch UI/I/O | effect fn reads |
| `LIST` / `MAP` | Collection sources | `ADD`, `SET ITEM`, `PUT` | collection accessors |

### 1.2 Edges

- **dep:** `from → to` means *from* read *to* during last compute/effect run.
- **sub:** inverse of dep (who to notify when *to* changes).
- Dynamic deps: recorded only while `g->computing != 0`.

### 1.3 Stale edge cleanup (v0.1)

Before each derived/effect (re)run, the runtime **must**:

1. For each prior dep `d` of node `n`, remove `n` from `d.subs`.
2. Reset `n.dep_len` to 0.
3. Re-register deps during the new run via reads.

**Guarantee:** No subscriber lists retain edges from superseded dynamic branches.

---

## 2. Flush protocol

A **flush** runs when `dirty_len > 0` and `batching == 0`.

### Wave order (fixed)

```text
W1  Propagate dirty → transitive subs marked dirty
W2  Recompute DERIVED (pure), ascending node id
W3  Run EFFECT, ascending node id
W4  Clear CELL/LIST/MAP dirty flags
    epoch++, flush_count++
W5  after_flush (layout/paint consumers)
```

### 2.1 Deterministic derived order

Within wave 2, derived nodes are considered in **strict ascending `LukeRxId` order** each iteration.  
Among nodes whose derived dependencies are clean, **lower id runs first**.

**Guarantee:** Same graph + same writes ⇒ same derived run order (single-threaded).

### 2.2 Batch coalescing

While `batching > 0`, writes mark dirty but **do not flush**.  
One `batch_end` ⇒ at most **one** flush for all batched writes.

**Guarantee:** `flush_count` increments by 1 per completed batch (not per write).

### 2.3 Cycle detection

If wave 2 makes no progress with pending derived nodes, the runtime:

1. Sets `cycle_tripped = 1`
2. Logs to stderr
3. Force-clears derived dirty flags
4. Returns `-1` from `luke_rx_flush`

---

## 3. Invalidation

A write to node `n`:

1. Updates value + `version++`
2. Marks `n` dirty (if applicable)
3. Recursively marks subscribers dirty (transitive)

Derived/effect nodes **never** write their dependents without going through a cell/collection write.

---

## 4. Scopes & disposal

- `scope_begin` tracks new nodes in `owned[]`.
- `scope_end(name)` disposes owned nodes: unlinks all edges, marks `dead = 1`.
- Dead nodes are skipped in flush and reads return defaults.

---

## 5. Instrumentation (v0.1)

| Counter | Meaning |
|---------|---------|
| `epoch` | Flush generations |
| `flush_count` | Completed flushes |
| `last_flush_derived` | Derived runs in last flush |
| `last_flush_effects` | Effect runs in last flush |
| `last_flush_deps_cleared` | Stale dep edges removed in last flush |
| `total_deps_cleared` | Cumulative stale-edge removals |
| `granular_paints` | UI row paints (Phase 5 tests) |

Build surface (introspection):

```luke
THE EPOCH
THE FLUSH COUNT
THE DERIVED RUN COUNT
THE EFFECT RUN COUNT
THE STALE EDGE COUNT
THE GRANULAR PAINT COUNT
```

---

## 6. Conformance

Programs under `examples/build/reactive_conformance_*.luke` assert v0.1+ guarantees on native.  
WASM parity: same programs compiled with `-target browser` must match native outputs (roadmap).

---

## 7. Scheduler 2.0 (v0.2)

### 7.1 Priority lanes

Effects run in **ascending priority** (lower number = sooner):

| Priority | Kind | Surface |
|----------|------|---------|
| `UI` (0) | User-visible / interactive | `BIND`, `BIND LIST`, `BIND OPACITY`, `WHEN REACTIVE` |
| `NORMAL` (1) | Derived compute | `THE x IS …` |
| `BACKGROUND` (2) | Deferred work | `BIND BACKGROUND`, `WHEN BACKGROUND REACTIVE` |

Within the same priority, **ascending node id** breaks ties (deterministic).

### 7.2 Nested flush coalescing

While `flushing == 1`, cell/collection writes **must not** re-enter `luke_rx_flush` synchronously.  
They set `pending_flush = 1` and increment `deferred_flush_count`.

One outer flush turn:

1. Runs one or more internal **passes** until `pending_flush == 0` or `dirty_len == 0`
2. Increments `flush_count` **once**
3. Increments `epoch` **once**

**Guarantee:** `last_flush_passes >= 1`; nested writes during effects coalesce into the same turn.

### 7.3 Dirty dedup

`mark_dirty` on an already-dirty node is a no-op and increments `last_flush_dedup_hits` during flush.

### 7.4 Instrumentation (v0.2)

| Counter | Meaning |
|---------|---------|
| `last_flush_passes` | Internal passes in last turn |
| `deferred_flush_count` | Cumulative nested deferrals |
| `last_flush_dedup_hits` | Dedup hits last pass |
| `last_dirty_q_size` | `dirty_q` length at wave 1 |
| `last_flush_steps` | Timeline entries last turn |
| `ui_before_bg` | UI effect ran before any BACKGROUND effect |

Build surface:

```luke
THE FLUSH PASS COUNT
THE DEFERRED FLUSH COUNT
THE DIRTY DEDUP COUNT
THE SCHEDULER STEP COUNT
THE SCHEDULER UI BEFORE BACKGROUND
```

---

## 8. Granularity (v0.3)

### 8.1 Region paint

`luke_rx_ui_set_text_granular` and list row paints call `argus_paint_one` — only the touched Argus node is presented.  
Full `argus_paint` runs when `need_paint` is set (e.g. generic `BIND`).

### 8.2 Region layout

When `need_layout` is set (e.g. `BIND OPACITY`), `hanka_mark_region` marks the root box containing the leaf.  
`hanka_layout_dirty` relayouts only dirty roots when `keep_roots` is enabled (reactive UI).

### 8.3 Component subtree invalidation

On `scope_end`, external subscribers of owned nodes are marked dirty before disposal (`THE SUBTREE INVALID COUNT`).

---

## 9. Memory management (v0.4)

### 9.1 Disposal

`DESTROY COMPONENT` disposes owned nodes (`dead = 1`), unlinks all edges, clears compute/effect fns.  
Dead nodes are skipped in flush/reads (default values returned).

### 9.2 Weak reads

`THE WEAK VALUE OF cell` reads without registering a dependency edge during derived/effect compute.

### 9.3 Leak audit

`luke_rx_audit_graph` scans alive nodes for deps pointing at dead nodes, repairs them, and updates counters.

```luke
AUDIT REACTIVE
THE ALIVE NODE COUNT
THE DEAD NODE COUNT
THE DISPOSED COUNT
THE LEAK EDGE COUNT
THE WEAK READ COUNT
```

### 9.4 Weak effects

`WHEN REACTIVE WEAK cell CHANGES DO` registers an effect whose reads do not create dependency edges (same as weak reads for the whole effect body).

### 9.5 Scope GC

On `scope_end` / `UNMOUNT COMPONENT`, closed scope frames with no owned nodes are compacted from the scope table (`THE SCOPE GC COUNT`, `THE SCOPE FRAME COUNT`).

---

## 10. DevTools (v0.5)

### 10.1 Why-changed

`THE WHY ROOT OF derived` walks deps to the nearest source cell (BFS, min id tie-break).  
`THE WHY DEPTH OF derived` is hop count to that root.  
`TRACE WHY cell` logs the chain to stderr and increments `THE WHY TRACE COUNT`.

`THE LAST WRITE ID` records the most recent cell/collection write.

### 10.2 Live graph

`DUMP REACTIVE GRAPH` prints alive nodes to stderr (`THE GRAPH DUMP COUNT`).  
Counters: `THE GRAPH CELL COUNT`, `THE GRAPH DERIVED COUNT`, `THE GRAPH EFFECT COUNT`, `THE GRAPH EDGE COUNT`.

### 10.3 Timeline export

After flush, scheduler steps are readable via:

```luke
THE TIMELINE STEP ID AT 0
THE TIMELINE STEP WAVE AT 0
```

Wave `2` = derived, `3` = effect.

---

## 11. Non-normative (future)

Not yet specified: macrotask queues, parallel reactions, time-travel, field-level object tracking, Hanka region invalidation spec.

See [`REACTIVE_ROADMAP.md`](./REACTIVE_ROADMAP.md).
