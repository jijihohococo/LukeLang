# Mimo Compiler (C++) — DEPRECATED

> **Do not use for new work.**  
> Canonical runtime: [`../vm/`](../vm/).  
> Delete plan: [`../docs/LEGACY.md`](../docs/LEGACY.md).

Mimo is a frozen C++ → JavaScript emitter kept only until Phase C of the legacy delete plan. It will be removed from the tree; do not fix features here — port demos to `examples/build/` and `vm/` instead.

## Historical build (unsupported)

```bash
g++ -std=c++17 -O2 -o mimo.exe src/main.cpp src/compiler.cpp src/diagnostics.cpp src/util.cpp
```

Stale Windows `*.obj` products must never be committed (covered by root `.gitignore`).
