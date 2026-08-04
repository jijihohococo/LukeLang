# Luke Native Runtime

This is the **real** LukeLang execution path: a C++ bytecode VM with its own heap and mark-sweep garbage collector. It does **not** transpile to JavaScript or require Node.

## Quick start

```bash
cd vm
make
make test
```

## Architecture

| Piece | Role |
| --- | --- |
| `compiler.cpp` | Luke source → bytecode |
| `vm.cpp` | Stack bytecode interpreter |
| `heap.cpp` | Object allocator + mark-sweep GC |
| `object.hpp` | Classes, instances, contracts, closures, upvalues |

## Supported (native)

- Core: `SPEAK`, variables, arithmetic, lists, `IF` / `WHILE`
- **Functions** with call frames + recursion
- **Closures** — nested functions capture enclosing locals via upvalues
- **Blueprints:** `FOLLOWS`, `HAS`, `WHEN BORN`, `METHOD`, `NEW`, `ASK TO`, `SELF`, `CALL PARENT`
- **CONTRACT** / `IMPLEMENTS` with arity-aware compliance checks
- **ALWAYS** static fields and methods (`Creature.totalCount`, `ASK Creature TO count`)
- **PRIVATE** / **SECRET** fields and methods (enforced at runtime)

## Examples

| File | Covers |
| --- | --- |
| `examples/native/hello.luke` | Basics |
| `examples/native/functions.luke` | Functions / recursion |
| `examples/native/oop.luke` | Inheritance |
| `examples/native/advanced_oop.luke` | Contracts, statics, privacy |
| `examples/native/closures.luke` | Closures |
| `examples/native/privacy.luke` | Secret methods via public API |

## Roadmap

1. ~~Functions~~
2. ~~Blueprints~~
3. ~~Contracts / ALWAYS / privacy / closures~~
4. Modules, richer standard library
5. Retire JS as the default execution engine
