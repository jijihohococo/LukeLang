# Legacy stacks — deprecation and delete plan

> **Canonical implementation:** [`vm/`](../vm/) (`luke BUILD` / `luke SHOW`)  
> **Do not** grow `main.js`, `luke.js`, or `mimo/`.

LukeLang currently has three runnable paths. Only one is the product.

| Path | What it is | Status |
|------|------------|--------|
| **`vm/`** | Native Build → C/WASM + Play bytecode VM | **Canonical** — all new work |
| **`main.js` / `luke.js`** | Node JS transpiler / runner | **Deprecated** — historical demos only |
| **`mimo/`** | C++ → JS emitter | **Deprecated** — reference / Windows-era front-end |

## Why this matters

Parallel implementations confuse newcomers, duplicate docs (`getting_started` still taught `main.js`), and burn maintenance attention. “Reference only” without a delete date becomes permanent clutter.

## Delete plan

### Phase A — redirect (done with this plan)

- README + getting started point at `vm/build/luke`
- CI tests **only** `vm/` (`make test`)
- `mimo/README.md` and this doc mark JS paths as freeze / delete-bound
- No new features, examples, or npm scripts for `main.js` / `mimo/`

### Phase B — freeze (now)

- Treat `main.js`, `luke.js`, `package.json` `luke`/`build`/`show` scripts, and `mimo/src/**` as **read-only**
- Bugs fixed only if they block a documented historical demo; prefer porting the demo to `examples/build/`

### Phase C — remove (next cleanup)

Delete when CI on `vm/` has been green on `main` for a short stretch and no open issue depends on JS emit:

1. `main.js`, `luke.js` (and any sibling JS compiler bits unused by `scripts/`)
2. `mimo/` entirely (including any resurrected `*.obj` build products)
3. npm scripts that invoke the JS transpiler; keep `package.json` only if still needed for unrelated tooling, or slim it
4. Docs links that still mention `node main.js` / Mimo as a supported path

Keep `scripts/luke_browser_loader.cjs` and `scripts/run_wasi.cjs` — those serve **`vm/`** browser/WASI, not the legacy transpiler.

### Phase D — history note

After delete, leave a short “Removed legacy JS emitters” note in the changelog / commit message. No need to keep empty stub directories.

## Decision rule

If a change would only make sense on `main.js` or `mimo/`, **don’t make it** — implement it in `vm/` or drop the use case.
