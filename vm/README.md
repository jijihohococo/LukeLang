# Luke Native Runtime

**Build mode** is the real language (native, no GC). **Play mode** is the VM convenience layer.

```bash
make
./build/luke BUILD ../examples/build/hello.luke -o build/hello && ./build/hello
./build/luke SHOW  ../examples/native/hello.luke
make test   # Play + Build suites
```

## Modes

| Command | Engine | Memory |
| --- | --- | --- |
| `SHOW` / bare `.luke` | Bytecode VM | Mark-sweep GC |
| `BUILD [-o out]` | Luke → C → `cc -O2` | Bump arena only |
| `BUILD -target wasm` | Luke → C → WASI clang | Bump arena only |

See [`../docs/BUILD_MODE.md`](../docs/BUILD_MODE.md). Needs `LUKE_WASI_SDK` or `.tools/wasi-sdk` for WASM.

## Layout

| Path | Role |
| --- | --- |
| `src/build_c.cpp` | Build compiler (Luke → C) |
| `runtime/luke_rt.h` | Tiny no-GC arena runtime |
| `runtime/luke_std.h` | Files / JSON / HTTP C helpers for Build |
| `stdlib/` | `std/files`, `std/json`, `std/http` Luke modules |
| `src/compiler.cpp` + `vm.cpp` | Play VM |
| `src/main.cpp` | CLI |

## Examples

- `examples/build/` — must `BUILD`
- `examples/native/` — Play feature demos (closures, contracts, …)
