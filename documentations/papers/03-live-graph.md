# The LukeLang Live Graph

A technical paper on end to end reactivity from the database row to the screen pixel.

## Abstract

The Live Graph extends the LukeLang Reactive Engine across the network and into the database. Where a client side reactive engine answers the question when this value changes what needs to update inside a single process, the Live Graph answers it for the whole stack. A change to a database row propagates through a server side reactive cell, across the wire, into a client side cell, and finally to a single repainted pixel, with no query refetching, no cache invalidation, and no subscription plumbing written by the application. This paper describes the thesis, the architecture, the incremental view maintenance that keeps database backed cells current, the wire protocol and its resume behavior, the causal history that enables time travel, and the honest status of each tier. It also describes the two properties that fall out of the same structure: authorization checked by the compiler, and a tamper evident audit trail.

## 1. Thesis

Mainstream stacks glue three separate worlds together. The database holds state. The server reads it and shapes it. The client displays it. Between these worlds sit queries, caches, invalidation rules, subscriptions, refetching, and diffing, and the great majority of that machinery exists only to answer one question: when something changes, what needs to update.

The LukeLang Reactive Engine already answers that question inside the client. The Live Graph answers it for the whole stack. The thesis, stated plainly, is that in LukeLang the application never fetches, never invalidates, never subscribes, and never diffs. It declares dependencies once, and change finds its own way from the row to the pixel.

## 2. Architecture

The Live Graph is one dependency graph that spans four tiers.

```
database row  ->  server cell  ->  wire  ->  client cell  ->  pixel
```

The database tier holds the source of truth. A server cell is a reactive cell whose value is maintained from a database query. The wire carries changes from server to client. A client cell receives the changes and participates in the client side reactive graph exactly as any other cell. The pixel is the leaf, a bound node that repaints when the client cell changes.

The distinction from a conventional stack is that these are not four disconnected systems joined by application code. They are segments of a single reactive graph. A change entering at the database tier propagates through the same dependency mechanism that the Reactive Engine uses within the client, extended across process and network boundaries.

## 3. Database Backed Cells

### 3.1 Declaration

A database backed cell is declared with a watch. The watch names a query over the database, and the result becomes a reactive cell. When the underlying rows change, the cell updates, and everything that depends on the cell updates in turn.

### 3.2 Incremental view maintenance

The naive way to keep a query backed cell current is to rerun the query whenever anything might have changed. This does work proportional to the size of the query result on every change. The Live Graph instead maintains the result incrementally. It keeps a maintained snapshot of the query result and updates that snapshot in response to the specific rows that changed, rather than recomputing the whole result from scratch. This is incremental view maintenance, a well studied technique in database systems, applied here as the mechanism that keeps a server cell current.

### 3.3 Change detection and joins

The engine detects change at the database using a version signal, so it recomputes only when the data actually changed. For queries that join tables, the current implementation recomputes the join when either side changes and maintains the resulting cache. True differential dataflow, in which deltas propagate through the join without recomputation of the whole result, is the deeper form of this work and is identified as future work. The distinction is stated plainly because the difference between recompute on change and delta propagation is the difference between a working system and an optimal one.

## 4. The Wire

### 4.1 Server sent events

The wire uses server sent events. When a server cell changes, the server pushes the new value to subscribed clients over a long lived connection. On the client, the arriving value is written into a client cell using the same entry point that any other value write uses, so the client side reactive graph treats a pushed update identically to a local one.

### 4.2 Declarative subscription

The application does not write the transport. A subscription is declarative. The program states that a client cell follows a server endpoint, and the runtime owns the connection, the reception of events, and the write into the cell. There is no fetch loop and no timer in application code. This is the property that distinguishes push based reactivity from polling: the differentiator is not that data arrives, it is that no plumbing to make it arrive appears in the program.

### 4.3 Resume

Network connections drop. The wire assigns an identifier to each event and supports resume from the last seen identifier, so that a client which reconnects continues from where it left off rather than losing or duplicating updates. This ordering and resume behavior is the foundation on which correct time travel and audit are built, because both depend on a well ordered history of changes.

## 5. Causal History and Time Travel

Because change flows through an explicit graph, the runtime can record the causal history of the application: which change caused which downstream change. This history is the basis for time travel. A client can scrub its display back to an earlier point in the recorded history and replay forward. A prototype of this scrub behavior is implemented, with a client side history buffer and a user interface for stepping backward and forward.

The history is more than a debugging convenience. When it is hash chained, so that each entry commits to the previous one, it becomes tamper evident. This property is used directly by the authorization model described in the companion paper on authentication, where every access to protected data is an entry in the same causal history and the history therefore serves as an audit trail that can be replayed and verified.

## 6. Properties That Fall Out of the Structure

The Live Graph is not only a data synchronization mechanism. Because it is one compiler known graph spanning the stack, other properties follow from the same structure.

The first is authorization. Since the compiler knows the path from a database row to a pixel, it can require that data marked as protected only flows to a client that is scoped to the current user, and it can reject a program that would expose protected data on an unscoped path. This turns a forgotten authorization check into a compile error. The mechanism is described in full in the authentication paper.

The second is audit. Since the history of change is recorded and hash chained, every access to protected data is already a node in the graph. A query over that history answers who saw a given row and when, and the hash chain makes the answer tamper evident. Compliance reporting becomes a query over a structure that already exists rather than a separate logging system.

The third, which is future work, is parallelism. The graph encodes which computations are independent, because independent computations share no dependencies. That independence is exactly the information a scheduler needs to run reactions across multiple cores without data races. The dataflow structure of the Live Graph is proposed as the route to safe multicore execution without adopting a shared memory concurrency model.

## 7. Status and Limitations

The Live Graph has advanced from thesis to running system across several tiers, and it is honest to state exactly where each tier stands. Database backed cells through watch are implemented, with incremental view maintenance through a maintained cache and change detection through a version signal. Join queries recompute on change and maintain a cache, with true differential dataflow identified as future work. The wire uses server sent events with identifier ordering and resume from the last seen identifier, and declarative subscription removes transport code from the application. Time travel has a working prototype with a client history buffer and a scrub interface, and wiring the server and client scrub together through the shared history log is in progress. Production hardening of the wire, including backpressure, heartbeat and timeout, and authorization of the subscription channel, is open work. The multicore parallelism that the structure enables is future work.

## 8. Related Work

The Live Graph combines several established lines of work. Incremental view maintenance and differential dataflow come from database systems, where keeping a materialized view current without full recomputation is a long standing problem. Client server synchronization and local first systems have explored keeping a client current with server state. Fine grained reactivity comes from user interface systems. The contribution of the Live Graph is to unify these into a single reactive graph that spans the database, the server, the wire, and the client, expressed in one language, so that end to end reactivity is a property of the program rather than an assembly of separate systems.

## 9. Conclusion

The Live Graph is the maximal expression of the idea that the language understands change. It takes the reactive dependency graph that operates within a single client and extends it across the wire and into the database, so that a change to a row propagates to exactly the pixel that depends on it with no application written plumbing. It keeps database backed cells current with incremental view maintenance, carries changes over a resumable wire, records a causal history that supports time travel and audit, and provides the structure from which compiler checked authorization and tamper evident audit follow. The parts that are complete run and are tested. The parts that remain, chiefly true differential joins, wire hardening, and multicore parallelism, are identified honestly as the distance between a working system and a complete one.
