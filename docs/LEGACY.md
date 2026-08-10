# Legacy stacks — removed

> **Canonical implementation:** [`vm/`](../vm/) (`luke BUILD` / `luke SHOW`)

On 2026-08-10 (Phase C), the parallel JS emitters were deleted from the tree:

| Removed | Was |
|---------|-----|
| `main.js`, `luke.js` | Node JS transpiler / CLI |
| `mimo/` | C++ → JS emitter |
| `grammar/token.js` | JS token map for the transpiler |
| `SHOW.cmd` | Windows launcher for JS emit |
| `out.js`, `examples/*.js` companions | Generated JS artifacts |

`scripts/luke_browser_loader.cjs` and `scripts/run_wasi.cjs` remain — they serve **`vm/`** browser/WASI, not the old transpiler.

## Rule

If a change would only make sense on a JS emitter, **don’t make it** — implement it in `vm/` or drop the use case.

Historical `.luke` demos under `examples/` (e.g. `oop_full.luke`) may still exist as source; run them with `vm/build/luke SHOW … --vm` or port to `examples/build/`.
