# Luke Native Runtime

This is the **real** LukeLang execution path: a C++ bytecode VM with its own heap and mark-sweep garbage collector. It does **not** transpile to JavaScript or require Node.

## Quick start

```bash
cd vm
make
./build/luke SHOW ../examples/native/hello.luke
./build/luke SHOW ../examples/native/counter.luke
./build/luke SHOW ../examples/native/functions.luke
./build/luke SHOW ../examples/native/oop.luke
```

## Architecture

| Piece | Role |
| --- | --- |
| `compiler.cpp` | Luke source → bytecode |
| `vm.cpp` | Stack bytecode interpreter |
| `heap.cpp` | Object allocator + mark-sweep GC |
| `value.hpp` / `object.hpp` | Tagged values + class/instance layouts |

Objects (strings, arrays, functions, **classes**, **instances**) live on the Luke heap. Numbers and bools are unboxed. The GC traces the VM stack, globals, and constant pools as roots.

## Supported subset (native)

- `SPEAK` / `SAY` / `YELL` / `SHOUT`
- `MY NAME IS x SET TO expr` / `SET x TO expr`
- Numbers, booleans, quoted strings, `AND` concatenation
- `ADD` / `SUBTRACT` / `MULTIPLY` / `DIVIDE`
- Comparisons: `EQUALS`, `IS LESS THAN`, `IS GREATER THAN`
- `IF ... DO` / `END IF`
- `WHILE ... DO` / `END WHILE`
- `MAKE LIST WITH ...` and `ITEM AT i OF list`
- **Functions:** `THIS IS FUNCTION` / `GIVE BACK` / `ASK name WITH args`
- **Blueprints (Luke-owned OOP):**
  - `BLUEPRINT Name [FOLLOWS Parent] DO` … `END CLASS`
  - `HAS field [SET TO value]`
  - `WHEN BORN WITH args DO` … `END BORN`
  - `METHOD name WITH args DO` … `END METHOD`
  - `NEW Class WITH args`
  - `ASK obj TO method WITH args`
  - `SELF` / `SELF.field` / `SET SELF.field TO …`
  - `CALL PARENT method WITH args`

## Not yet on the native path

- `CONTRACT` / `IMPLEMENTS` (skipped for now)
- `ALWAYS` static fields/methods
- True private field enforcement
- Closures over outer locals

## Roadmap

1. ~~Functions (`THIS IS FUNCTION` / `GIVE BACK`) as first-class VM call frames~~
2. ~~Objects / blueprints with Luke-owned layouts (not JS classes)~~
3. Contracts, statics, privacy; better allocator
4. Retire JS as the default execution engine; keep optional JS emit only for interop
