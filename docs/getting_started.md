# Getting Started with LukeLang

LukeLang is **Build-first**: conversational syntax → native / WASM via `vm/`.

## Install

1. Clone the repo and enter it:
   ```bash
   git clone https://github.com/lucasdmarshall/LukeLang.git
   cd LukeLang
   ```
2. Build the Luke toolchain (needs a C++17 compiler + `make`):
   ```bash
   cd vm && make
   ```
3. Optional: for `-target wasm|browser`, install [WASI SDK](https://github.com/WebAssembly/wasi-sdk) under `.tools/wasi-sdk` (or set `LUKE_WASI_SDK`).

Node is only required for headless browser/WASI smoke tests (`scripts/*.cjs`), not for native `BUILD`.

## Your first program

Create `hello.luke`:

```luke
SPEAK "Hello, World!"
```

## Run it

From `vm/`:

```bash
./build/luke BUILD ../path/to/hello.luke -o hello && ./hello
# or:
./build/luke SHOW ../path/to/hello.luke
```

You should see:

```text
Hello, World!
```

Browser ship:

```bash
./build/luke PUBLISH WEB ../examples/build/frontend_widgets.luke -o /tmp/luke_web
```

## Next

- Language of record: [`BUILD_MODE.md`](./BUILD_MODE.md)
- Frontend stack: [`FRONTEND_ROADMAP.md`](./FRONTEND_ROADMAP.md)
- Reactive: [`REACTIVE.md`](./REACTIVE.md)
- Legacy JS paths (`main.js` / `mimo/`): [`LEGACY.md`](./LEGACY.md) — do not use for new work
