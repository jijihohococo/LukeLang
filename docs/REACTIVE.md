# LukeLang Reactive Architecture

> **Status:** Phases 1–8 shipped; Phases 9–11 production roadmap (correctness, scheduler, granularity)  
> **Normative spec:** [`REACTIVE_SPEC.md`](./REACTIVE_SPEC.md) · **Roadmap:** [`REACTIVE_ROADMAP.md`](./REACTIVE_ROADMAP.md)
> **Identity:** *Lukelang understands change.*  
> **Not:** a React/Vue-style framework bolted onto the language  
> **Is:** language + runtime primitive — one dependency graph for UI, backend, game, animation

## Thesis

A state change in Luke is **not** an app-level “event you handle.”  
It is a **dependency change the runtime already understands.**

```text
change → invalidate dependents → recompute → layout (Hanka) → paint (Argus)
```

Frontend UI updates, backend derived caches, game entity/animation triggers, and timelines all share **one** reactive mechanism.

Argus does **not** become a reactive framework.  
Hanka does **not** own semantic state.  
They **consume** invalidation/update signals from the Reactive Runtime.

---

## Placement in the stack

```text
                    LUKE LANG (conversational surface)
                            │
                      Compiler / AST
                            │
                   Reactive Analyzer (static deps)
                            │
                     Reactive Runtime  ←── center
                            │
              ┌─────────────┼──────────────┐
              │             │              │
             UI          Backend          Game
              │             │              │
            Hanka        Network         Physics
              │          Database        Entities
            Argus          Files          Audio
              │             │              │
              └─────────────┴──────────────┘
                            │
                       Scheduler
                            │
                     Native / WASM (Build AOT, arena)
```

Build mode remains **AOT truth** (Luke → C → native/WASI/browser, bump arena, no GC).  
Reactive cells live in the arena; the graph is explicit, not tracing-GC magic.

---

## Core primitives

### 1. Cell (remembered value)

Surface (target conversational forms — may soft-land as aliases of today’s `MY NAME IS`):

```luke
REMEMBER the user's name AS "Lucas"
SPEAK "Hello, " AND THE USER'S NAME
CHANGE the user's name TO "Alex"
```

Internally:

```text
name  →  Reactive Cell  →  Dependency Graph edges
```

Writers mark the cell dirty. Scheduler walks dependents. **No full app rerender.**

### 2. Derived (computed cell)

```luke
REMEMBER the price AS 100
REMEMBER the quantity AS 3
THE total IS the price MULTIPLIED BY the quantity
```

```text
price ─────┐
           ├──► total (derived)
quantity ──┘
```

Library-level `useMemo` / Vue `computed` become a **runtime primitive**, not a framework API.

### 3. Effect (side-effect subscription)

```luke
WHENEVER the user's name CHANGES DO
  SAVE the name
END WHENEVER
```

Effects must be scheduled separately from pure derived recomputation.  
**Cycle detection** is mandatory (save must not mutate `name` in a loop).

### 4. Reactive events (first-class continuations)

Today’s `WHEN "id" IS CLICKED` / `WHEN FETCH … IS READY` become graph entry points:

```text
CLICK(login) → set loading → START FETCH
FETCH READY  → set loading false → GO TO dashboard | show error
```

Async work is **not** Promise-shaped in user code; it is event → work → result cell → dependents.

### 5. Reactive collections

Structural change (add) vs item/slot change (set/put) — dependents and Argus consumers
can paint **per index**, not the whole list by default.

```luke
REMEMBER players AS LIST
BIND LIST players AS "row"   // paints row_0, row_1, …
ADD "Ada" TO players
SET ITEM 1 OF players TO "Zoe"   // only that row dirty-paints
```

`change_kind` / `last_index` on the collection node drive `luke_rx_ui_paint_list`.
Maps use the same touch model via `PUT` / `GET`.

### 6. Reactive scopes / components

A component is a **scope**: state + deps + handlers + UI tree + effects.  
Destroy → unsubscribe (owned cells/effects disposed; aligns with arena doctrine).

```luke
BEGIN COMPONENT Counter
  REMEMBER count AS 0
  …
END COMPONENT
DESTROY COMPONENT Counter
```

---

## Runtime data model (Phase 1 target)

```c
typedef uint32_t LukeRxId;

typedef enum LukeRxKind {
  LUKE_RX_CELL = 1,
  LUKE_RX_DERIVED = 2,
  LUKE_RX_EFFECT = 3
} LukeRxKind;

typedef struct LukeRxNode {
  LukeRxId id;
  LukeRxKind kind;
  int dirty;
  /* edges: deps[] / subs[] stored densely in arena tables */
} LukeRxNode;
```

Sketch lives in `vm/runtime/luke_reactive.h` (Phase 1 API implemented).

**Invariants**

1. Writes go through `luke_rx_write` / conversational `CHANGE` / `INCREASE` on remembered cells  
2. Derived reads register the reader as a dependent (dynamic tracking) **or** use compile-time edges  
3. Effects never run mid-propagation; they run in the effect wave after pure recompute  
4. Cycles: compile-time where possible; runtime tripwire otherwise  

### Phase 1 Build surface

```luke
REMEMBER price AS 100
REMEMBER quantity AS 3
THE total IS price MULTIPLIED BY quantity
CHANGE quantity TO 4
INCREASE quantity BY 1
BEGIN REACTIVE BATCH
  CHANGE price TO 200
  CHANGE quantity TO 5
END REACTIVE BATCH
FLUSH REACTIVE
SPEAK total
```

Smoke: `examples/build/reactive_core.luke` · cycle tripwire: `examples/build/reactive_cycle.luke`

### Phase 2 UI surface

```luke
REMEMBER username AS ""
BIND "greeting" TO "Welcome, " AND username
WHEN "name" IS CHANGED DO
  CHANGE username TO THE VALUE OF "name"
END WHEN
UPDATE "greeting" WITH "Welcome, " AND username
PAINT DIRTY
```

Pipeline: event → `CHANGE` cell → effect (`BIND`) → `argus_set_text` → dirty paint.  
**No** `CLEAR THE SCREEN`. Text-only updates skip Hanka relayout.

Demo: `examples/build/reactive_greeting.luke`

### Phase 3 component surface

```luke
BEGIN COMPONENT Counter
  REMEMBER count AS 0
  BIND "counter-label" TO "Count: " AND count
END COMPONENT

WHEN "counter-inc" IS CLICKED DO
  INCREASE count BY 1
END WHEN

DESTROY COMPONENT Counter
```

`BEGIN COMPONENT` opens a reactive scope; cells/effects inside are owned.  
`DESTROY COMPONENT` unsubscribes those nodes.

Demos: `reactive_counter_scope.luke` · `reactive_counter.luke`

### Phase 4 async surface

```luke
REMEMBER body AS ""
REMEMBER status AS 0
REMEMBER ready AS 0

START FETCH "demo" GET "luke://stub/profile" INTO body STATUS status READY ready

WHEN FETCH "demo" IS READY DO
  CHANGE loading TO 0
END WHEN

BIND "body-label" TO "body: " AND body
```

On fetch ready: runtime writes result cells (batched) **then** runs the WHEN body — a continuation edge into the reactive graph. No Promise API in user code.

`luke://…` URLs finish locally (deterministic tests). Demo: `reactive_fetch.luke` 

---

## Scheduler (the heart)

One batch, ordered waves — never “random immediate fire” for every write:

```text
Events / writes
      ↓
   Batch
      ↓
 Invalidate (mark dirty)
      ↓
 Propagate (topo / depth)
      ↓
 Recompute (derived, pure)
      ↓
 Effects (I/O, save, network kick)
      ↓
 Layout  (Hanka — affected regions)
      ↓
 Paint   (Argus — dirty nodes only)
```

Multiple sources in one turn (price + quantity + resize + fetch + animation tick) coalesce into **one** layout/paint pass when possible.

---

## Compiler: static + dynamic hybrid

Preferred:

| Kind | How edges appear |
| --- | --- |
| Static | Analyzer sees `THE total IS price * quantity` → edge metadata in Build IR |
| Dynamic | Branches / collections / late binding → runtime tracking on read |

Build IR should grow a `rx` section (ids, kinds, static edges) so WASM/native bootstraps the graph without rediscovering every edge at runtime.

---

## UI pipeline (Argus + Hanka)

```text
User Input
   ↓
Argus event (CLICKED / CHANGED / SUBMITTED)
   ↓
Reactive cell write
   ↓
Dependency graph
   ↓
Invalidate text/layout consumers
   ↓
Hanka relayouts affected frames only
   ↓
Argus paints dirty nodes only
```

**No virtual DOM diff as the source of truth.**  
Semantic deps (Luke) → layout deps (Hanka) → paint deps (Argus).

Existing beachhead wiring that Reactive will absorb:

- `THE VALUE OF` / `SET` / `WHEN … IS CLICKED|CHANGED|SUBMITTED`
- `START FETCH` / `WHEN FETCH … IS READY`
- Hanka nested boxes → Argus dirty paint

---

## Cross-domain same graph

| Domain | Source | Dependent |
| --- | --- | --- |
| Web UI | input cell | greeting text → Hanka → Argus |
| Backend | DB row / list | derived “active users”, dashboard stats |
| Game | `player.health` | health bar, death, injured animation |
| Animation | `menu.open` / `progress` | timeline → frames → Argus |

Animation progress is itself a cell (`0.0 → 1.0`) that drives layout/paint dependents.

---

## Philosophy surface

Avoid “setState culture.” Prefer verbs that mutate cells:

```luke
INCREASE the count BY 1
CHANGE the user's name TO "Alex"
```

Developer describes **what changed**. Runtime decides **what must react**.

Debugging target (later DevTools):

```text
WHY DID THIS CHANGE?
username ← LoginForm.input
greeting ← username
Hanka relayout ← greeting
Argus paint ← TextNode#greeting
```

---

## Non-goals (near term)

- Replacing Build AOT / arena with a GC reactive heap  
- Making Argus/Hanka frameworks that own app state  
- Shipping Phase 6–8 before Phase 1 scheduler works  
- Full static analysis of every dynamic edge on day one  

---

## Implementation roadmap

### Phase 1 — Reactive Core *(shipped)*

**Deliverables**

- Cells + derived + invalidation + single-threaded scheduler  
- Arena-friendly graph tables in `luke_reactive.h`  
- Build surface: `REMEMBER` / `THE x IS …` / `CHANGE` / `INCREASE` / batch + flush  
- Smoke: change quantity → total recomputes; unaffected cells untouched  

**Acceptance**

1. Derived updates without manual `PAINT` for non-UI tests  
2. Two writes in one turn batch to one recompute pass  
3. Cycle on derived graph fails loudly  

### Phase 2 — UI bridge *(shipped)*

Input/event → cell → Hanka region invalidate (text-only skips) → Argus dirty paint.  
Greeting demo without full `CLEAR THE SCREEN`.

**Deliverables**

- TEXT cells (`luke_rx_cell_text` / `luke_rx_write_text`)  
- `BIND "id" TO expr` effect → Argus text + `need_paint`  
- `UPDATE` / `PAINT DIRTY` · flush after-wave via `luke_rx_ui_after_flush`  
- Demo: `examples/build/reactive_greeting.luke`

### Phase 3 — Component scopes *(shipped)*

Counter component: local cells, handlers, auto cleanup on scope end.

**Deliverables**

- `luke_rx_scope_begin` / `luke_rx_scope_end` — owned node tracking + dispose/unlink  
- Build: `BEGIN COMPONENT Name` … `END COMPONENT` · `DESTROY COMPONENT Name`  
- Demos: `reactive_counter_scope.luke`, `reactive_counter.luke`

### Phase 4 — Async in the graph *(shipped)*

`START FETCH` results write cells; `WHEN FETCH READY` is a continuation edge.

**Deliverables**

- `START FETCH … INTO body STATUS status READY ready`  
- Auto prelude on FETCH READY: batch-write cells → flush → user WHEN body  
- Synthesize FETCH READY handler when INTO is used without an explicit WHEN  
- `luke://` stub fetch for offline/deterministic tests  
- Demo: `examples/build/reactive_fetch.luke`

### Phase 5 — Collections *(shipped)*

Granular list/map invalidation; `BIND LIST` paints `{prefix}_{index}` using `change_kind`.

**Deliverables**

- `REMEMBER name AS LIST|MAP`
- `ADD` / `SET ITEM n OF list TO v` / `PUT` → `luke_rx_list_*` / `luke_rx_map_*`
- `BIND LIST name AS "prefix"` — item updates paint one Argus row
- `THE GRANULAR PAINT COUNT` for tests
- Demos: `examples/build/reactive_list.luke`, `reactive_list_ui.luke`

### Phase 6 — Animation *(shipped)*

Timeline progress as NUMBER cells → `BIND OPACITY` / layout consumers.

**Deliverables**

- `START TIMELINE "id" FOR ms MILLISECONDS FROM a TO b INTO cell` (native: stepped sync)
- `WHEN TIMELINE "id" IS FINISHED DO` · browser rAF ticks via `timeline_start`
- `BIND OPACITY "element" TO expr`
- Demos: `reactive_timeline.luke`, `reactive_timeline_ui.luke`

### Phase 7 — Game *(shipped)*

Entity scopes with dotted field cells (`Player.health`).

**Deliverables**

- `BEGIN ENTITY Name` … `END ENTITY` · `DESTROY ENTITY Name`
- Field cells as `Entity.field` in the dependency graph
- Demo: `reactive_entity.luke`

### Phase 8 — Backend *(shipped)*

Query result TEXT cells; `REFRESH QUERY` re-runs SQL and invalidates dependents.

**Deliverables**

- `REMEMBER x AS QUERY ON db AS "sql"` · `REFRESH QUERY x`
- `luke_rx_query_refresh` over `luke_db_query_text`
- Demo: `reactive_query.luke`

### Phase 9 — Correctness *(shipped v0.1)*

Production-grade scheduler guarantees before new features.

**Deliverables**

- Normative [`REACTIVE_SPEC.md`](./REACTIVE_SPEC.md) v0.1 (flush waves, stale-edge cleanup, batch coalescing)
- Scheduler instrumentation: `THE FLUSH COUNT`, `THE STALE EDGE COUNT`, `THE DERIVED RUN COUNT`, …
- `luke_rx_clear_deps` — unlink superseded dynamic edges before derived/effect rerun
- Conformance programs: `examples/build/reactive_conformance_{batch,stale,order}.luke`
- Roadmap: [`REACTIVE_ROADMAP.md`](./REACTIVE_ROADMAP.md)

### Phase 10 — Scheduler 2.0 *(shipped v0.2)*

Priority lanes, dirty dedup, nested flush coalescing, timeline export.

**Deliverables**

- Effect priority: UI (`BIND`, `WHEN REACTIVE`) before `BACKGROUND` (`BIND BACKGROUND`, `WHEN BACKGROUND REACTIVE`)
- Deferred flush during active flush (`THE DEFERRED FLUSH COUNT`, `THE FLUSH PASS COUNT`)
- Dirty dedup + queue instrumentation (`THE DIRTY DEDUP COUNT`)
- Scheduler timeline counters (`THE SCHEDULER STEP COUNT`, `THE SCHEDULER UI BEFORE BACKGROUND`)
- Conformance: `reactive_conformance_{priority,nested,dedup}.luke`

### Phase 11 — Granularity *(shipped v0.3)*

Partial layout/paint and component subtree invalidation.

**Deliverables**

- `argus_paint_one` — region-targeted Argus paint
- `hanka_layout_dirty` / `hanka_mark_region` — partial Hanka relayout
- Component subtree invalidation (`THE SUBTREE INVALID COUNT`)
- Introspection: `THE REGION PAINT COUNT`, `THE REGION LAYOUT COUNT`
- Conformance: `reactive_conformance_subtree.luke`

### Phase 12 — Memory *(in progress)*

Dead nodes, weak reads, leak audit.

**Deliverables**

- `luke_rx_audit_graph` + leak edge repair
- Weak reads: `THE WEAK VALUE OF x` (no dependency edge)
- Introspection: `THE ALIVE NODE COUNT`, `THE DEAD NODE COUNT`, `THE DISPOSED COUNT`, `THE LEAK EDGE COUNT`
- `AUDIT REACTIVE` statement
- Conformance: `reactive_conformance_{memory,weak}.luke`

---

## Relation to current engines

| Doc | Role vs Reactive |
| --- | --- |
| [`BUILD_MODE.md`](./BUILD_MODE.md) | AOT host; IR will carry `rx` metadata |
| [`ARGUS.md`](./ARGUS.md) | Paint consumer of dirty regions |
| [`HANKA.md`](./HANKA.md) | Layout consumer of frame invalidation |
| [`PRODUCTION_WEB.md`](./PRODUCTION_WEB.md) | Ship path; Reactive makes partial updates production-grade |

---

## Signature line

> **Lukelang understands change.**

Not “Luke has state management.”  
The compiler and runtime know **what depends on what** — and only that work runs.
