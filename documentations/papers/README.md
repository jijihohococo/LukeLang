# LukeLang Technical Papers

Long form technical papers describing the core systems of LukeLang. Each paper is self contained and states the honest status of the system it describes, distinguishing what is implemented and tested from what is proven prototype and what is future work.

| Paper | Subject |
| --- | --- |
| [01 Reactive Engine](./01-reactive-engine.md) | The change propagation model at the core of the language: cells, the dependency graph, the scheduler, memory management without a garbage collector, error isolation, tooling, and performance. |
| [02 Frontend Engines: Argus and Hanka](./02-frontend-argus-hanka.md) | The layout engine Hanka and the rendering engine Argus, the pipeline that connects them, surgical paint, accessibility and motion, and the Path A direction that delegates layout and paint to the browser. |
| [03 Live Graph](./03-live-graph.md) | End to end reactivity from the database row to the screen pixel: database backed cells, incremental view maintenance, the resumable wire, causal history, and the properties that follow from the structure. |
| [04 Authentication: Secure by Compiler](./04-auth.md) | Authentication and authorization as language properties: secure defaults, compiler enforced authorization and flow safety, and reactive and time travel capabilities. |
| [05 Request Pipeline: Middleware as Compiler Checked Capabilities](./05-middleware.md) | Routes as a declared table, middleware as required capabilities with checked ordering, form bodies and schema as declarations, and the pipeline mistakes that become compile errors. |
| [06 OAuth and Authentication Flows as Compiler Verified State Machines](./06-oauth.md) | Delegated authorization and multi step authentication as declared flows, with the property that a flow cannot reach completion without its required verification enforced at compile time. |

## Reading order

The papers can be read in any order, but they build on one another. The Reactive Engine is the foundation. The Frontend Engines and the Live Graph both extend the reactive model, the first to the screen and the second across the stack. Authentication builds on all three, using the compiler known graph, the reactive Live Graph, and the recorded history. The Request Pipeline and the OAuth flow papers extend the authentication model to the backend, applying the same principle, that what the compiler can see the compiler can guarantee, to routing, middleware, and multi step authentication flows.

## A note on honesty

These papers describe an actively developed language. Where a system is a proven prototype rather than a complete implementation, the papers say so. This is deliberate. The value of a technical description is lost if the reader cannot trust the boundary between what works and what is planned.
