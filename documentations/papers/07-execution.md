# LukeLang Execution: The Build Pipeline and the Differential Ledger

A technical paper on how LukeLang source becomes running behavior, and how reactive queries execute as maintained differences rather than repeated work.

## Abstract

Execution in LukeLang has two faces. The first is how a program becomes a running artifact: a Build pipeline compiles the shared program tree to C and from there to a native binary, to WebAssembly, or to a browser bundle, with an arena memory model and no garbage collector at Build time, and a bytecode virtual machine as a portable fallback. The second is how the reactive parts of a program execute at runtime: a query declared over a database does not re run on every change but is maintained incrementally by a ledger of triggers that apply the difference a mutation makes. This paper describes both, states the correctness bar the differential ledger is held to, and draws the honest boundary around the query shapes that are maintained differentially versus those that fall back to a correct recompute.

## 1. Two Meanings of Execution

A language paper about execution usually means one thing: how the source runs. In LukeLang the word carries a second, equally important meaning, because the differentiating feature of the language is that reactive state stays current without the program asking. So this paper treats execution as both the compile and run pipeline and the runtime maintenance of reactive queries. The two are connected: the same Build pipeline that turns a program into a native binary also emits the trigger ledger that maintains its reactive queries, so both are properties of one compiler.

## 2. The Build Pipeline

### 2.1 From program tree to C

Build mode consumes the program tree produced by the front end described in the architecture paper and emits C. The emission is a single pass over flattened statements. Control flow, functions, and scoped blocks are nested nodes in the tree, and each emits the corresponding C construct. Expressions are typed during emission so that arithmetic, text, and integer semantics are decided at compile time rather than deferred to a runtime value tag.

### 2.2 Targets

The emitted C is compiled to one of three targets. The native target produces an ordinary binary. The WebAssembly target produces a module for a system interface host. The browser target produces a module plus a boot script and a page, so that a program that declares an interface becomes a served page. The target is a compile time choice; the program does not change.

### 2.3 Memory without a garbage collector

At Build time the runtime uses an arena. Allocations are bump allocations into a region that is released as a whole when its scope ends. There is no per object collection and no collector thread. This is the correct model for a compiled program with clear scopes, and it is why Build mode has no pause behavior. The reactive runtime layered on top keeps its own stable allocations where identity must survive arena growth, which is why the scene tree index and the cell table are not invalidated when the arena grows.

### 2.4 Build integrity

Because the emitted C includes runtime headers whose layout must match across translation units, the build tracks header dependencies so that a changed header forces recompilation of every object that includes it. This closes a class of failure where a stale object compiled against an older structure layout links against newer code and corrupts memory at runtime. The failure is silent when it happens and expensive to diagnose, so the build is arranged to make it impossible rather than to detect it after the fact.

### 2.5 The virtual machine fallback

Alongside Build mode there is a bytecode virtual machine with its own garbage collector. It exists as a portable fallback and as the substrate for dynamic execution where ahead of time compilation is not wanted. The two execution modes share the front end and diverge only at the back end, so a program has one meaning whether it is compiled or interpreted.

### 2.6 Source level debugging

The emitted C carries line directives that map generated lines back to the original program, so a source level debugger can set a breakpoint at a program line, step in terms of the program rather than the generated C, and inspect state. A debug adapter exposes this over a standard protocol. Execution is therefore observable at the level the author wrote, not the level the compiler produced.

## 3. The Differential Ledger

### 3.1 The problem with recompute

A query declared over a database defines a value that depends on rows. The naive way to keep that value current is to re run the query whenever the database changes. This is correct and simple and it does not scale, because the cost of every update is the cost of the whole query regardless of how small the change was. The reactive promise of the language is that the cost of an update is proportional to the change, not to the data. Recompute breaks that promise.

### 3.2 Triggers that apply a difference

Instead, Build mode emits a ledger of database triggers that maintain a cached result as rows change. A mutation fires a trigger that applies the difference the mutation makes to the cache, so an insert adds its contribution, a delete removes it, and an update adjusts it. The maintained value is read from the cache, which is already current, so a read costs nothing beyond the read. The triggers are gated so that they fire only for mutations that can affect the result, which keeps writes that do not matter cheap.

### 3.3 Filters as gated contributions

For a query with a filter, the ledger keeps a bag of contributing rows keyed by row identity, and the trigger that maintains it is gated on the filter predicate evaluated against the new and old row images. This makes the maintenance sound for filters beyond equality: a threshold, a pattern, a membership, or a compound condition each become a gate on the trigger, so a row enters or leaves the bag exactly when it enters or leaves the filter. A function call inside a filter is treated as a value, not a column, so a predicate over a function of a column is maintained rather than mis parsed into an invalid trigger.

### 3.4 Joins as row identity keyed bags

A join is maintained as a bag keyed by the identities of the rows on both sides, so that a join that produces several rows for one key is maintained as several contributions rather than collapsed to a scalar. A partner mutation adjusts exactly the contributions that involve it. Chains across three or more tables are maintained through an equality closure that pins the changed row and probes its partners, so a change on any table in the chain updates the result without re running the join.

### 3.5 The correctness bar

The ledger is held to an adversarial equivalence bar. For each supported shape, the maintained value after a sequence of mutations must equal a fresh recompute of the same query over the final state. The mutation sequences are chosen to be hostile: crossing a filter threshold in both directions, deleting a partner, reassigning a join key, inserting duplicates, closing and reopening a group. A maintained value that ever disagrees with the ground truth recompute is a defect, not a tolerance. This bar is what separates a real differential from a differential that is right on the easy cases and quietly wrong on the hard ones.

### 3.6 The honest fallback

Not every query shape is maintained differentially. A filter that contains a subquery falls back to a correct recompute rather than a bag, because the subquery's own dependencies are outside the row image the trigger sees. Broader join shapes, non equality join conditions, and result lists that are expressions rather than columns are outside the maintained set today. The important property is that the fallback is correct: an unsupported shape recomputes and returns the right answer, it does not return a stale one. The boundary is between fast and correct on one side and merely correct on the other, never between correct and wrong.

## 4. Why the Two Faces Are One System

The Build pipeline and the differential ledger are not separate features that happen to live in the same compiler. The ledger is emitted by the same pass that emits the rest of the program, from the same tree, with the same type information. A query's filter predicate is compiled once, into both the read path and the trigger gates, so the maintained result cannot drift from the query's stated meaning. This is the payoff of a single front end: the thing that runs and the thing that keeps it current are generated together and cannot disagree.

## 5. Status and Limitations

The Build pipeline compiles to native, WebAssembly, and browser targets, with an arena model and no collector at Build time, header dependency tracked builds, source level debugging, and a bytecode virtual machine fallback. The differential ledger maintains point and multi row joins, equality and inequality and pattern and membership and compound filters, and chains across three or more tables, and it is held to the recompute equivalence bar. The honest remainder is the set of query shapes that fall back to recompute: subquery filters, broader and non equi join conditions, and expression only result lists. These are correct today and differential tomorrow; none of them is a wrong answer, and none of them requires a second intermediate representation to fix.

## 6. Related Work

Incremental view maintenance is a long studied database technique, and differential dataflow systems maintain the results of relational queries under changing input. LukeLang's contribution is not the theory but the placement: the maintenance ledger is emitted by the language compiler from the same program tree that produces the binary, so incremental maintenance is a language level guarantee rather than a database feature the programmer wires up. The arena model and the ahead of time compilation follow the tradition of systems languages that trade a collector for scoped ownership.

## 7. Conclusion

Execution in LukeLang means two things that are really one. A program compiles through a shared tree to C and to a native, WebAssembly, or browser artifact, with scoped arena memory and no collector at Build time. The reactive queries in that program execute as maintained differences, kept current by a trigger ledger that applies the change a mutation makes and is held to equality with a fresh recompute. Where a shape is not yet maintained, it recomputes correctly. The single front end that emits both the program and its ledger is why the running artifact and its live state cannot disagree.
