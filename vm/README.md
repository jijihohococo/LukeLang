# Luke Native Runtime

This is the **real** LukeLang execution path: a C++ bytecode VM with its own heap and mark-sweep garbage collector. It does **not** transpile to JavaScript or require Node.

## Quick start

```bash
cd vm
make
./build/luke SHOW ../examples/native/hello.luke
./build/luke SHOW ../examples/native/counter.luke
./build/luke SHOW ../examples/native/functions.luke
```

## Architecture

| Piece | Role |
| --- | --- |
| `compiler.cpp` | Luke source → bytecode |
| `vm.cpp` | Stack bytecode interpreter |
| `heap.cpp` | Object allocator + mark-sweep GC |
| `value.hpp` | Tagged values (nil/bool/number/heap objects) |

Objects (strings, arrays) live on the Luke heap. Numbers and bools are unboxed. The GC traces the VM stack, globals, and constant pool as roots.

## Supported subset (v0 native)

- `SPEAK` / `SAY` / `YELL` / `SHOUT`
- `MY NAME IS x SET TO expr` / `SET x TO expr`
- Numbers, booleans, quoted strings, `AND` concatenation
- `ADD` / `SUBTRACT` / `MULTIPLY` / `DIVIDE`
- Comparisons: `EQUALS`, `IS LESS THAN`, `IS GREATER THAN`
- `IF ... DO` / `END IF`
- `WHILE ... DO` / `END WHILE`
- `MAKE LIST WITH ...` and `ITEM AT i OF list`
- **Functions:** `THIS IS FUNCTION` / `MAKE FUNCTION` / `RECIPE`, `GIVE BACK`, `ASK name WITH args`
  - Params and `MY NAME IS` inside functions are real call-frame locals
  - Recursion works (see `examples/native/functions.luke`)

## Not yet on the native path

Classes (`BLUEPRINT`), methods (`ASK obj TO method`), contracts, and closures over outer locals still only exist on the legacy JS path. Those are next — Luke-owned object layouts on this VM.

## Roadmap

1. ~~Functions (`THIS IS FUNCTION` / `GIVE BACK`) as first-class VM call frames~~
2. Objects / blueprints with Luke-owned layouts (not JS classes)
3. Growable heap generations / better allocator
4. Retire JS as the default execution engine; keep optional JS emit only for interop
