# Luke Build Mode — The Real Language

> **Play** (`luke SHOW`) is a convenience: GC’d bytecode VM for demos and exploration.  
> **Build** (`luke BUILD`) is the language of record: conversational syntax → **native code**, **no GC**, Rust-class lightness.

This is Path A: Python-shaped versatility on the surface, Zig/Rust-shaped cost underneath.

## Commands

```bash
luke SHOW  examples/build/hello.luke              # prefers Build (native temp); --vm forces Play
luke BUILD examples/build/hello.luke              # Build — native binary, no GC
luke BUILD examples/build/hello.luke -o hello
luke BUILD examples/build/hello_wasm.luke -target wasm -o hello.wasm
luke BUILD examples/build/hello_browser.luke -target browser -o hello_web
# run wasm (WASI): node scripts/run_wasi.cjs hello.wasm
# run browser wasm headless: node scripts/luke_browser_loader.cjs hello_web.wasm
# or open hello_web.html in a browser
```

Build emits C, then compiles with the system C compiler (`cc -O2`), or the WASI SDK when `-target wasm|browser`.
Browser also writes `*.html` + `luke_browser_loader.js` (copied beside the wasm) for `<script>` tags.

## IMPORT + stdlib + packages

```luke
IMPORT "./critter.luke"          # relative module
IMPORT std/files                 # read/write TEXT files
IMPORT std/json                  # JSON tree helpers
IMPORT std/http                  # httpGet (native)
IMPORT luke/greeter              # package from luke_modules/greeter
```

`std/*` resolves from `vm/stdlib/`. Relative paths are next to the entry file.

### Packages (`luke/<name>`)

Look up order:
1. `luke_modules/` next to the source file
2. `./luke_modules`, `LUKE_PACKAGES` (colon-separated roots)
3. Package dir must contain `luke.pkg` (with `entry=…`), or `main.luke`, or `<name>.luke`

```
luke_modules/greeter/luke.pkg    # entry=main.luke
luke_modules/greeter/main.luke
```

`IMPORT package:greeter` is an alias for `IMPORT luke/greeter`.

Foreign FFI imports (C/JS/Python bridges) are intentionally **not** magic `IMPORT numpy` — parked for later; C wrappers will come first.

| Module | Helpers |
| --- | --- |
| `std/files` | `readFile`, `writeFile`, `fileExists` |
| `std/json` | `jsonParse`, `jsonGet`, `jsonIndex`, `jsonLen`, `jsonHas`, `jsonAsText` / `Number` / `Flag`, `jsonStringify`, `jsonString` |
| `std/http` | `httpGet` (native via curl; empty on WASI) |
| `std/server` | `httpListen`, `httpAccept`, `httpReply`, `httpPath` / `Method` / `Query` / `Body` |
| `std/sqlite` | `dbOpen`, `dbExec`, `dbQuery`, `dbClose` (auto `-lsqlite3`) |
| `std/args` | `argCount`, `getArg` |
| `std/env` | `getEnv`, `setEnv` |
| `std/paths` | `cwd`, `pathJoin`, `pathBasename`, `pathDirname` |
| `std/process` | `shell`, `exitWith` |
| `std/js` | `jsSetText`, `jsSetHtml`, `jsGetValue`, `jsFetch`, `jsOnClick`, `jsLoadFont`, `jsAddStyle`, `jsSetTitle` |
| `luke/…` | Your packages under `luke_modules/` (`luke PKG init <name>`) |

### Browser page ownership (conversational)

```luke
NAME THE PAGE "LukeLang"
BRING FONT "Syne" FROM "./fonts/syne-700.woff2"   # local pack → @font-face + copy
WEAR STYLE """
  body { font-family: Syne, sans-serif; }
"""
FILL "root" WITH """
  <h1>LukeLang</h1>
  <button id="go">Go</button>
  <p id="out"></p>
"""
WHEN "go" IS CLICKED DO
  FILL "out" WITH "Still LukeLang."
END WHEN
```

`-target browser` emits HTML with Luke title/CSS/body/fonts **baked in**, wasm beside it, and **inlines** `vm/runtime/luke_browser_boot.js` (runtime — not app JS). Dist has no `luke_browser_loader.js`.

See `sample/landing.luke`.

### Collections + problems (conversational)

```luke
MY NAME IS nums AS LIST
ADD "one" TO nums
SPEAK ITEM 0 OF nums
SPEAK HOW MANY IN nums

MY NAME IS bag AS MAP
PUT "name" TO "Luke" IN bag
SPEAK GET "name" FROM bag

ATTEMPT DO
  GIVE UP WITH "could not finish"
OTHERWISE WITH problem DO
  SPEAK problem
END ATTEMPT
```

### TEST

```luke
TEST "math" DO
  MAKE SURE ADD 1 AND 1 EQUALS 2
END TEST
```

```bash
luke TEST examples/build/collections_test.luke
```

### Packages

```bash
luke PKG init mylib
luke PKG install echo
luke PKG publish mylib
luke PKG lock          # writes luke.lock
```

### Arena scopes

```luke
IN ARENA DO
  MY NAME IS tmp AS TEXT SET TO "ephemeral"
  SPEAK tmp
END ARENA
```

Bump pointer is restored at `END ARENA` — request/frame-scoped memory without GC.

## Guarantees (Build)

| Rule | Meaning |
| --- | --- |
| **No GC** | Memory comes from stack, statics, or a bump **arena** freed in bulk |
| **Fixed layouts** | `BLUEPRINT` fields are a C struct — not open hash maps |
| **Known types** | Locals/fields are `NUMBER`, `FLAG`, `TEXT`, or a blueprint type |
| **Native code** | Ahead-of-time C → machine code (and later WASM) |
| **Same voice** | `SPEAK`, `MY NAME IS`, `ASK`, `BLUEPRINT` still work |

## Types (Luke words)

| Luke | Build representation |
| --- | --- |
| `NUMBER` | `double` |
| `FLAG` | `int` (0/1) |
| `TEXT` | `LukeText { ptr, len }` (arena or literal) |
| `JSON` | `LukeJson *` (arena tree — parse / get / stringify) |
| `LIST` | `LukeList *` (arena-backed text items) |
| `MAP` | `LukeMap *` (arena-backed text keys/values) |
| `SERVER` / `REQUEST` | HTTP server / request handles |
| `DATABASE` | SQLite handle |
| `BLUEPRINT Foo` | `typedef struct Foo { ... } Foo` |

Inference (v0):
- `42` → `NUMBER`
- `"hi"` / wordy strings → `TEXT`
- `TRUE` / `FALSE` → `FLAG`
- `ASK jsonParse WITH …` → `JSON`
- `HAS name SET TO "..."` → field `TEXT`
- `HAS count SET TO 0` → field `NUMBER`
- `HAS x AS NUMBER` / `AS TEXT` / `AS FLAG` / `AS JSON` — explicit when needed
- First assignment to a local fixes its type; later `SET` must match
- Function args/arity and `GIVE BACK` types are checked
- Optional: `THIS IS FUNCTION f … GIVES BACK TEXT DO`

## Memory (Luke words)

| Word | Meaning in Build |
| --- | --- |
| *(default locals)* | Stack / registers |
| `NEW` instances | Allocated in the **thread arena** (bump); live until arena reset/program end |
| `TEXT` concat / dynamic strings | Arena-backed |
| `IN ARENA` … `END ARENA` | Checkpoint + reset bump pointer (scoped bulk free) |
| `DROP` *(roadmap)* | Early release of a single value |

There is **no** mark-sweep collector in Build binaries. Peak memory is predictable: stack + arena high-water mark.

## Blueprints

```luke
BLUEPRINT Dog FOLLOWS Animal DO
  HAS sound SET TO "Woof!"
  WHEN BORN WITH name DO
    SET SELF.name TO name
  END BORN
  METHOD speak DO
    SPEAK SELF.name AND " says " AND SELF.sound
  END METHOD
END CLASS
```

Lowers to roughly:

```c
typedef struct Dog {
  Animal base; /* or flattened parent fields */
  LukeText sound;
} Dog;

void Dog_speak(Dog *self);
Dog *Dog_born(LukeArena *a, LukeText name);
```

- `ASK buddy TO speak` → `Dog_speak(buddy)` (static dispatch when type known)
- `CALL PARENT speak` → `Animal_speak((Animal*)self)`
- `PRIVATE` fields → C name mangling; only methods of that blueprint may touch them
- `ALWAYS HAS` → static/globals on the blueprint’s C file scope

## What Play allows that Build may reject

- Adding undeclared fields at runtime
- Truly dynamic `ASK` on unknown types (Build wants a known blueprint type)
- Unlimited runtime mutation of object shapes
- Relying on GC to clean cycles (Build uses arenas — don’t build immortal graphs by accident)

Play remains for sketching. **Shipping artifacts should `BUILD`.**

## Roadmap toward “Python everywhere + Rust light”

1. ~~Build → C + arena runtime~~
2. ~~Clearer Luke-voice Build errors; `AS TYPE` annotations~~
3. ~~Packages / relative `IMPORT` + `std/files` + `std/json`~~
4. ~~`luke BUILD -target wasm` (WASI)~~
5. ~~Richer typechecking; fuller JSON; thin HTTP GET~~
6. ~~Browser-oriented WASM packaging (`-target browser`)~~
7. ~~Package registry (`IMPORT luke/<name>` + `luke_modules/`)~~
8. ~~SHOW prefers Build; Play VM is the compatibility layer (`--vm`)~~
9. ~~Tooling stdlib (args/env/paths/process) + `luke PKG init`~~
10. ~~Browser JS bridge (`std/js`)~~
11. ~~`IN ARENA` / `END ARENA` scopes~~
12. ~~Remote package registry (`luke PKG install` + `registry/index.json`)~~
13. ~~Foreign imports (`IMPORT c:` + `FOREIGN FUNCTION`)~~
14. ~~Build IR shared frontend (expand/soften for Play; `luke IR` dump)~~
15. ~~LIST / MAP + ATTEMPT / OTHERWISE + `luke TEST`~~
16. ~~`std/server` + `std/sqlite` + browser fetch/click~~
17. ~~`luke PKG publish` + `luke.lock`~~
18. Richer remote registry (signed packages)
19. Explicit Python bridges (beyond C FFI)
20. Optional: emit Play bytecode opcodes directly from Build IR nodes

### Rendering / layout (engine track)

- **Argus (rendering engine, now):** [`ARGUS.md`](./ARGUS.md) — `PLACE` / `PAINT THE SCREEN`, Luke scene → DOM presentment.
- **Layout engine (future):** [`LAYOUT_ENGINE.md`](./LAYOUT_ENGINE.md) — parked; do not build until paint/dirty path is solid.

```luke
IMPORT std/argus
PLACE "brand" AS TEXT AT 48, 420 SIZE 900, 80 SAY "LukeLang"
PAINT THE SCREEN
```

```bash
luke BUILD examples/build/argus_demo.luke -target browser -o build/argus_demo
```

## Philosophy

Conversational syntax is the **UI**.  
Build-mode layouts, types, and arenas are the **truth**.  
That split is how Luke can feel like Python and weigh like Rust.
