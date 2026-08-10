# Advanced Topics in LukeLang

Prefer [`BUILD_MODE.md`](./BUILD_MODE.md) for the language of record. This page is a short pointer — older JS-transpile guidance was removed with the legacy emitters ([`LEGACY.md`](./LEGACY.md)).

## Browser / JS host

Build `-target browser` ships WASM + a thin DOM boot (`scripts/luke_browser_loader.cjs`). Luke owns layout/paint (Hanka → Argus); the page is a host surface, not the source of truth.

## Errors

- Build: `ATTEMPT` / `GIVE UP` / `OTHERWISE`, plus reactive isolation when using the reactive runtime.
- Play VM: runtime errors surface through the SHOW path.

## Modules

```luke
IMPORT "./critter.luke"
IMPORT std/json
IMPORT luke/greeter
```

See BUILD_MODE for packages (`luke PKG …`).

## Contributing

[`contributor_guide.md`](./contributor_guide.md) — work in `vm/`, land on `main`, run `make test`.
