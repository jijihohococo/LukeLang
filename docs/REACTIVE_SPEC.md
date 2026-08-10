# LukeLang Reactive Specification v0.1

> **Status:** Normative draft (Phase 9 — correctness foundation)  
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

Programs under `examples/build/reactive_conformance_*.luke` assert v0.1 guarantees on native.  
WASM parity: same programs compiled with `-target browser` must match native outputs (roadmap).

---

## 7. Non-normative (future)

Not in v0.1: priority queues, parallel reactions, time-travel, weak refs, field-level object tracking, Hanka region invalidation spec.

See [`REACTIVE_ROADMAP.md`](./REACTIVE_ROADMAP.md).
