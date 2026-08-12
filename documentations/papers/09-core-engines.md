# LukeLang Core Engines: The Substrate and How the Engines Compose

A technical paper on the small set of primitives beneath every LukeLang engine, and on the way four engines compose into one dataflow from a database row to a screen pixel.

## Abstract

LukeLang is built from four engines: the reactive engine that propagates change, the incremental maintenance engine that keeps database backed values current, the layout engine Hanka, and the rendering engine Argus. Each has its own paper. This paper is about what they share and how they fit together. Beneath all of them is a small substrate — a scoped arena allocator, a compact text value, a stable identity index, a cell, and a trigger — and above all of them is a single composition in which a change to a stored row travels through the maintenance engine, the wire, the reactive engine, and the rendering engine to become one repainted node. The paper describes the substrate, the contract each engine exposes, and the composition, and it is honest about which seams are complete and which are proven prototypes.

## 1. Why a Paper About the Seams

The individual engine papers describe each engine in depth. But a system is not only its parts; it is the way the parts meet. A reactive engine that could not drive a renderer surgically would not deliver constant time visible updates. A maintenance engine whose output did not look like a cell would need an adapter between the database and the reactive graph. The value of LukeLang's engines is as much in their shared shape as in their individual behavior, and that shared shape is the subject here.

## 2. The Substrate

### 2.1 The arena

The lowest primitive is a region allocator. Allocations bump a pointer into a region, and the region is released as a whole when its scope ends. Every engine allocates transient structure from an arena, which is why none of them needs a collector at Build time and why the cost of ending a scope is constant rather than proportional to what it held. Where an engine needs identity to survive the arena growing, it holds that structure in a stable allocation outside the bump region; this is the single exception that the substrate makes, and it is made deliberately in exactly the two places identity matters.

### 2.2 The text value

Text is a compact value of a pointer and a length rather than a heap owned string with a terminator. This makes passing text between engines and across the boundary to the embedder cheap and copy free, and it is the reason a value can travel from a database read through a cell to a painted label without repeated allocation.

### 2.3 The identity index

Both the reactive cell table and the rendering scene tree need to find a thing by its identity in constant time as the number of things grows. The substrate provides an open addressed index from identity to slot, backed by a stable allocation so that growth does not invalidate it. The same primitive that makes reactive mounting linear makes scene mounting linear; it is one idea used twice.

### 2.4 The cell

A cell is a value that other values can depend on. Reading a cell inside a computation records a dependency; writing a cell schedules the dependents. The cell is the atom of the reactive engine, and it is also the shape that the maintenance engine's output takes, which is what lets a database backed value enter the reactive graph without an adapter.

### 2.5 The trigger

A trigger is a gated action that fires on a database mutation. It is the atom of the maintenance engine. A trigger applies the difference a mutation makes to a cached contribution, and it is gated so that it fires only for mutations that can matter. The trigger and the cell are the two ends of the live dataflow: the trigger maintains a value as rows change, and the cell carries that value into the interface.

## 3. The Four Engines as Contracts

Each engine can be understood as a contract expressed in substrate terms.

The reactive engine's contract is that a write to a cell results, after a deterministic flush, in exactly the dependent effects running, each once, in a defined order. Its atoms are cells and effects; its guarantee is granularity and determinism.

The maintenance engine's contract is that a database backed cell equals a fresh recompute of its query after any sequence of mutations, for the query shapes it supports, and recomputes correctly for the rest. Its atoms are triggers and cached bags; its guarantee is that reading a live value is free because the value is already current.

Hanka's contract is that a set of layout terms resolves to an arrangement, either as absolute frames or as style level instructions the browser realizes. Its atom is the container and the leaf; its guarantee is that arrangement is separate from paint.

Argus's contract is that a change to one node updates one node on the host surface. Its atom is the scene node; its guarantee is surgical paint, that the cost of a visible update is the cost of the nodes that changed.

## 4. The Composition

The engines compose into one dataflow.

```
row  --trigger-->  server cell  --wire-->  client cell  --effect-->  Argus node  -->  pixel
```

A mutation to a stored row fires a trigger, which updates the server cell's cached value. The wire carries that change to the client over a resumable channel. On the client the change writes a cell, which schedules the effect that binds the cell to a scene node. The effect calls a single node update on Argus, and Argus paints that one node. No layer polls the layer before it; each is driven by the change arriving from the one before. The end to end property is that the cost of reflecting a database change on the screen is proportional to the change, not to the data or the size of the interface, because every engine in the chain is granular in the same way.

This is why the shared shape matters. Because the maintenance engine's output is a cell, it plugs into the reactive graph with no adapter. Because the reactive engine drives effects granularly, it drives Argus granularly. Because Argus paints one node, the granularity survives to the pixel. A single design choice — that a change should cost what the change is worth — is made once in each engine and preserved across every seam because the seams are built from the same atoms.

## 5. Where the Seams Are Complete and Where They Are Proven

The reactive to render seam is complete and is the most exercised: a bound value repaints exactly one node, asserted by a region paint count, in both the classic and the browser layout modes. The maintenance to reactive seam is complete for the supported query shapes and is held to the recompute equivalence bar. The database to wire to client seam is implemented end to end, with the wire resumable and fail closed; the parts of it that concern a full development time inspection of the live log are the least finished. The render seam to the browser, under the completed Path A, is verified by construction and by native harnesses rather than by an automated end to end test in a real document, which is the same honest boundary the frontend paper draws. The composition is real and the atoms are shared; the remaining work is coverage that exercises the whole chain in a browser, not a missing seam.

## 6. Related Work

Reactive user interface libraries provide a cell and an effect but stop at the client and leave the database to a separate query layer. Incremental view maintenance and differential dataflow systems maintain query results but are databases, not languages, and their output is not a reactive cell that an interface binds to. LukeLang's contribution is to make these the same substrate: a trigger maintained value is a cell, a cell drives an effect, an effect paints a node, and one allocator and one text value and one identity index underlie all of them, so the whole path from row to pixel is one system rather than four connected by adapters.

## 7. Conclusion

LukeLang's engines share a substrate of an arena, a text value, an identity index, a cell, and a trigger, and they compose into a single dataflow from a stored row to a painted pixel. Each engine keeps the same promise, that a change costs what the change is worth, and because the seams between them are built from shared atoms the promise survives from the database to the screen. The engines are individually described elsewhere; read together, they are one machine whose parts were shaped to fit.
