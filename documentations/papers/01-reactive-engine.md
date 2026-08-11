# The LukeLang Reactive Engine

A technical paper on the change propagation model at the core of LukeLang.

## Abstract

The LukeLang Reactive Engine is the runtime that gives the language its signature property: the program declares what depends on what, once, and the runtime computes the smallest amount of work required whenever anything changes. This paper describes the engine in full. It covers the data model of cells and derived values, the dependency graph, the scheduler and its priority lanes, memory management under an arena allocator with no garbage collector, the error isolation model, the developer tooling surface, and the measured performance characteristics. The central result is that a single state change repaints exactly one node in constant time, independent of scene size, and that this property is verified by a conformance suite that runs in continuous integration.

## 1. Motivation

Most user interface systems spend the majority of their runtime answering one question over and over: when a piece of state changes, what needs to be recomputed and redrawn. Traditional immediate mode systems answer this by redrawing everything on every frame. Retained mode systems with virtual document trees answer it by rebuilding a shadow tree and diffing it against the previous one. Both approaches do work that scales with the size of the interface rather than with the size of the change.

The Reactive Engine takes a different position. It records the dependency structure of the program as an explicit graph. Because the graph is known, the runtime can walk from a changed value to exactly the set of computations and paint operations that depend on it, and touch nothing else. The work performed by an update is proportional to the number of things that actually depend on the changed value, not to the total size of the program state or the interface.

This is not a new idea in isolation. Spreadsheets have propagated changes through a dependency graph for decades. Modern reactive user interface libraries apply the same principle on the client. The contribution of LukeLang is to make this model a first class property of a compiled, garbage collector free language, and to extend it later across the network and into the database, a direction described in the companion paper on the Live Graph.

## 2. Data Model

### 2.1 Cells

A cell is the unit of reactive state. A cell holds a value and a list of the computations that currently depend on it. Writing to a cell marks its dependents as needing recomputation. Reading a cell inside a reactive computation records a dependency edge from that computation to the cell.

Cells carry a static type. LukeLang supports text cells, number cells, integer cells backed by 64 bit signed integers, and flag cells for boolean state. The type of a cell is fixed at declaration and checked at compile time.

### 2.2 Derived values

A derived value is a computation whose result is itself observable. A derived value reads one or more cells, produces a value, and can be depended on by other derived values or by effects. Derived values are the mechanism by which the graph acquires depth. A chain of derived values forms a path through the graph, and a change at the root of that path propagates forward only as far as values actually change.

### 2.3 Effects

An effect is a computation that reads reactive state and produces a side effect rather than a value. The canonical effect in LukeLang binds a cell or a derived value to a node in the interface, so that when the value changes the node repaints. Effects sit at the leaves of the dependency graph. They are the point at which reactive change becomes visible work.

## 3. The Dependency Graph

The dependency graph is a directed graph. Nodes are cells, derived values, and effects. Edges record the relation depends on. When a computation reads a cell, the runtime adds an edge from the computation to the cell, so the runtime always knows the current dependency set of every computation.

Dependencies are dynamic. A derived value that reads different cells on different runs will have different incoming edges after each run. The engine clears the stale dependency set of a computation before it runs and rebuilds the set during the run. This guarantees that the graph reflects the dependencies of the most recent execution and never carries edges from a branch that is no longer taken.

The graph is the single source of truth for propagation. When a cell is written, the runtime does not scan the program. It walks the outgoing edges of that cell, marks the reachable computations dirty, and hands the dirty set to the scheduler.

## 4. The Scheduler

### 4.1 Flush model

Writes do not run effects immediately. A write marks dependents dirty and schedules a flush. The flush is the unit of propagation. During a flush the scheduler processes the dirty set, recomputes derived values in dependency order, and runs the effects whose inputs changed. Batching writes into a flush means that several writes in the same logical step produce a single coordinated update rather than a cascade of partial updates.

### 4.2 Priority lanes

Not all reactive work is equally urgent. Painting a value the user is looking at should happen before recomputing a background aggregate. The scheduler supports priority lanes. Interface effects run in a high priority lane. Background computations run in a lower priority lane. The scheduler drains higher priority work first, so visible updates are not delayed by bulk work.

### 4.3 Deduplication and deferred flush

Within a flush the scheduler deduplicates dirty entries so that a value which becomes dirty through several paths is recomputed once. When an effect running during a flush writes a new value, that write can require another pass. The scheduler handles this with a deferred flush: it runs one or more internal passes within the same logical turn until no dirty work remains, then returns. This keeps nested updates deterministic and confined to a single turn.

### 4.4 Determinism

The scheduler processes work in dependency order. A derived value is never recomputed before the values it depends on. This ordering is specified in the normative reactive specification and exercised by conformance tests that assert deterministic output for graphs with shared dependencies, diamond shapes, and nested reactions.

## 5. Memory Management Without a Garbage Collector

LukeLang compiles to native code with arena memory and no garbage collector. This is a deliberate constraint of the language and it shapes the reactive engine. Reactive nodes cannot rely on a tracing collector to reclaim them, so the engine manages their lifetimes explicitly.

### 5.1 Scopes

Reactive state is grouped into scopes. A component scope owns the cells, derived values, and effects created within it. When the scope is disposed, its reactive nodes are disposed with it. This gives structured, predictable teardown that matches the lifetime of a component.

### 5.2 Weak references and audits

The engine supports weak reads and weak effects, which observe a value without extending its lifetime. It exposes counters for alive nodes, dead nodes, and disposed nodes, and it can audit the graph to detect and repair leaked edges. The audit reports a leak edge count so that a program can assert the absence of leaks in a test. Automatic scope garbage collection reclaims the reactive nodes of a scope when the scope is unmounted.

### 5.3 Why this matters

Explicit lifetime management is more work than relying on a collector, but it produces predictable memory behavior with no collection pauses. For a language whose promise is to ship like a systems language, predictable reclamation of reactive state is a requirement rather than a nicety.

## 6. Error Isolation

A reactive graph connects many independent computations. A failure in one effect must not corrupt the flush or silence unrelated effects. The engine isolates errors at the level of the individual derived value or effect. When one computation fails, the failure is recorded on that node, the flush continues, and sibling effects still run.

The engine supports asynchronous failure reporting for work that completes outside the current turn, retry and clear operations to recover a failed node, and error boundaries that contain failures within a region of the graph and can be reset. This gives an application a structured way to represent partial failure without tearing down the whole interface.

## 7. Developer Tooling

Because the engine owns the dependency graph and the history of changes, it can answer questions that are hard to answer in systems where change propagation is implicit.

The tooling surface includes graph statistics such as the cell count and edge count, a why changed trace that reports the root cause and depth of a change for a given node, per node records of the last write and the counts of dependencies and subscribers, a timeline of scheduler steps, and a live snapshot of the graph. The region paint count and granular paint count expose exactly how many nodes painted in the most recent flush and cumulatively, which turns the central performance claim into something a test can assert rather than something a benchmark merely suggests.

## 8. Performance

All numbers below are from native builds measured on the development host. They are reproduced by the benchmark program in the repository and guarded by the test target in continuous integration.

### 8.1 Granular update

A single one node update runs in constant time and paints exactly one node. Measured cost is on the order of ten microseconds per update, and the region paint count for the flush is one. The cost does not grow with the size of the scene, which is the defining property of the engine.

### 8.2 Mount and scale

Mounting one hundred thousand reactive nodes completes in tens of milliseconds and scales linearly with node count. Earlier revisions contained a quadratic cost in the scene table caused by a linear scan on every node insert. This was replaced with an open addressed hash index from identifier to node, which restored linear scaling. The lesson is recorded here because the benchmark that exposed the regression is the same benchmark that now guards against its return.

### 8.3 Comparison to full rebuild

Against a naive baseline that clears and rebuilds the entire scene, a targeted update is orders of magnitude cheaper. The exact ratio depends on the scene size and on whether one compares a single update or a batch of updates against one full rebuild. The honest and stable way to report the result is per update: one state change, one node repaint, constant time. Ratios against full rebuild are illustrative of the difference between constant and linear work, and they should be quoted against the naive rebuild baseline rather than against any named framework.

## 9. Status and Limitations

The engine is mature in its core and has open work at its edges. Correctness, the scheduler, granularity, memory management, error isolation, and the developer tooling surface are implemented and covered by conformance tests. Time travel and deterministic replay have a working prototype through the Live Graph but are not yet a fully specified part of the reactive specification. Compiler level optimization of the graph, such as elimination of dead reactions and generation of straight line update code for statically known regions, is future work. Parallel reactions and race safe cells are future work and are discussed in the Live Graph paper, where the dataflow structure of the graph is proposed as the basis for safe multicore scheduling.

## 10. Related Work

The engine draws on a long line of work. Spreadsheets established dependency driven recomputation. Fine grained reactive user interface libraries established constant time targeted updates on the client. Incremental computation and self adjusting computation established the theory of propagating changes through a computation graph. The contribution here is the combination: a fine grained reactive engine implemented in a compiled language with arena memory and no garbage collector, with an explicit graph that the runtime and the tooling can both inspect, positioned as the foundation for change propagation across an entire application stack.

## 11. Conclusion

The Reactive Engine is the part of LukeLang that earns the phrase the language understands change. It records dependencies as an explicit graph, propagates changes through that graph in dependency order under a batching scheduler with priority lanes, manages the lifetime of reactive state without a garbage collector, isolates failures, and exposes the whole structure to tooling. Its defining measured property is that one state change produces one node repaint in constant time, verified in continuous integration. Everything else in LukeLang that is described as reactive, from the frontend engines to the Live Graph to the authorization model, is built on this foundation.
