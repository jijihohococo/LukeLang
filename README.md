# LukeLang — Programming with Personality

LukeLang is a conversational language with a **Build-first** vision:
write like Python, ship like Rust.

| Mode | Command | What it is |
| --- | --- | --- |
| **Build** (real language) | `luke BUILD file.luke` | Native / WASM / browser — **no GC**, arena memory |
| **Show** (Build-first) | `luke SHOW file.luke` | Runs via Build when possible; Play VM fallback |
| **Play VM** | `luke SHOW file.luke --vm` | Bytecode VM + GC (compatibility layer) |

```bash
cd vm && make
./build/luke BUILD ../examples/build/hello.luke -o hello && ./hello
./build/luke BUILD ../examples/build/hello_browser.luke -target browser -o web/hello
./build/luke SHOW  ../examples/build/hello.luke
./build/luke SHOW  ../examples/native/closures.luke --vm
```

Read [`docs/BUILD_MODE.md`](docs/BUILD_MODE.md) for types, memory, packages, and browser packaging.

## Status

| Layer | State |
| --- | --- |
| Build → native C | Core + functions + blueprints + IMPORT + typechecks |
| Build → WASM | `-target wasm` (WASI) and `-target browser` (html/js glue) |
| Build stdlib | files, json, http, server, sqlite, args, env, paths, process, js |
| Packages | `IMPORT luke/<name>` + `luke PKG init|install|publish|lock` |
| Collections | `LIST` / `MAP` + `ATTEMPT` / `GIVE UP` / `OTHERWISE` |
| Tests | `luke TEST` + `MAKE SURE` / `TEST … END TEST` + GitHub Actions CI |
| Arena scopes | `IN ARENA` / `END ARENA` |
| Show | Prefers Build; Play VM is `--vm` / fallback |
| Legacy `main.js` / `mimo/` | Deprecated — see [`docs/LEGACY.md`](docs/LEGACY.md) |

## Quick examples

**Build (native/WASM/browser):** `examples/build/`  
**Play-only demos:** `examples/native/` (closures, contracts, …) — use `--vm` or SHOW fallback  
**Sample package:** `luke_modules/greeter`

## Design Principles

- Conversational keywords; words over symbols
- **Build** is the language of record (layouts, types, arenas)
- **Play VM** is the skateboard / compatibility layer
- Optional JS emit only as interop later — never the identity

## Roadmap

- Richer remote registry (versions, signing)
- Explicit Python bridges (beyond C FFI)
- Emit Play bytecode opcodes directly from Build IR nodes
- Replace line-based Build stmt matching with a real lexer/AST (see BUILD_MODE)

## Legacy JS path

`main.js` / `luke.js` / `mimo/` are **deprecated**. Do not teach or extend them.  
Deletion plan: [`docs/LEGACY.md`](docs/LEGACY.md). Prefer `vm/build/luke`.
