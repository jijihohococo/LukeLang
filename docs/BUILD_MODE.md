# Luke Build Mode — The Real Language

> **Play** (`luke SHOW`) is a convenience: GC’d bytecode VM for demos and exploration.  
> **Build** (`luke BUILD`) is the language of record: conversational syntax → **native code**, **no GC**, Rust-class lightness.

This is Path A: Python-shaped versatility on the surface, Zig/Rust-shaped cost underneath.

## Commands

```bash
luke SHOW  examples/build/hello.luke    # Play — interpret now
luke BUILD examples/build/hello.luke    # Build — native binary, no GC
luke BUILD examples/build/hello.luke -o hello
```

Build emits C, then compiles with the system C compiler (`cc -O2`).

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
| `BLUEPRINT Foo` | `typedef struct Foo { ... } Foo` |

Inference (v0):
- `42` → `NUMBER`
- `"hi"` / wordy strings → `TEXT`
- `TRUE` / `FALSE` → `FLAG`
- `HAS name SET TO "..."` → field `TEXT`
- `HAS count SET TO 0` → field `NUMBER`
- `HAS x AS NUMBER` / `AS TEXT` / `AS FLAG` — explicit when needed
- First assignment to a local fixes its type

## Memory (Luke words)

| Word | Meaning in Build |
| --- | --- |
| *(default locals)* | Stack / registers |
| `NEW` instances | Allocated in the **thread arena** (bump); live until arena reset/program end |
| `TEXT` concat / dynamic strings | Arena-backed |
| `IN ARENA` *(roadmap)* | Explicit arena scope |
| `DROP` *(roadmap)* | Early release / end scope |

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

1. ~~Build → C + arena runtime~~ (this milestone)
2. Infer more types; better errors in Luke’s voice
3. Packages / `IMPORT`
4. `luke build -target wasm`
5. Concurrency model + thin stdlib (files, JSON, HTTP) on Build ABI
6. Shrink Play to a thin compatibility layer — or generate Play bytecode *from* Build IR later

## Philosophy

Conversational syntax is the **UI**.  
Build-mode layouts, types, and arenas are the **truth**.  
That split is how Luke can feel like Python and weigh like Rust.
