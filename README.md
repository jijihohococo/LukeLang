# LukeLang — Programming with Personality

LukeLang is a conversational language with a **Build-first** vision:
write like Python, ship like Rust.

| Mode | Command | What it is |
| --- | --- | --- |
| **Build** (real language) | `luke BUILD file.luke` | Native binary via C — **no GC**, arena memory |
| **Play** (convenience) | `luke SHOW file.luke` | Bytecode VM + GC for instant demos |

```bash
cd vm && make
./build/luke BUILD ../examples/build/hello.luke -o hello && ./hello
./build/luke SHOW  ../examples/native/hello.luke
```

Read [`docs/BUILD_MODE.md`](docs/BUILD_MODE.md) for types, memory, and the Path A architecture.

## Status

| Layer | State |
| --- | --- |
| Build → native C | Core + functions + blueprints working |
| Play VM | Full feature sandbox (closures, contracts, …) |
| Legacy `main.js` / `mimo/` | Old JS emit — reference only |

## Quick examples

**Build (native):** `examples/build/` — hello, counter, functions, oop  
**Play (VM):** `examples/native/` — includes closures, contracts, privacy

## Design Principles

- Conversational keywords; words over symbols
- **Build** is the language of record (layouts, types, arenas)
- **Play** is the skateboard; Build is the car
- Optional JS emit only as interop later — never the identity

## Roadmap

- Expand Build type inference + errors in Luke’s voice
- Packages / `IMPORT`
- `luke build -target wasm` (frontend)
- Thin Build stdlib (files, JSON, HTTP)
- Keep shrinking the gap so Play is optional

## Legacy JS path

`node luke.js SHOW …` still exists for historical demos. Prefer `vm/build/luke`.
