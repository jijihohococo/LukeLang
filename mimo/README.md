# Mimo Compiler (C++) — Legacy JS emitter

> **Note:** Mimo currently compiles `.luke` → JavaScript. That is **not** the intended long-term LukeLang runtime.
> The native VM (own bytecode, heap, GC) lives in [`../vm/`](../vm/). New work should target `vm/`.

Mimo is an older C++ front-end for LukeLang that emits JavaScript. It remains here for reference while the native runtime catches up on OOP features.

## Features (v0.1)

- CLI that compiles a `.luke` file to `.js` beside it
- Line-based parser for core LukeLang constructs:
  - `BLUEPRINT ... DO` (class), `FOLLOWS`, `IMPLEMENTS` (subset)
  - `WHEN BORN WITH ... DO` and `METHOD __init__ WITH ... DO` (constructor)
  - `METHOD name WITH args DO` (methods)
  - `SPEAK`, `SET name OF SELF TO value`, `ASK obj TO method WITH args`
  - `CALL PARENT method WITH args` and `CALL PARENT method OF Ancestor WITH args`
- JavaScript code generation with `// Compiled by Mimo v0.1`
- Expressive diagnostics explaining errors and fixes

## Build

### Option A: MSVC (Visual Studio Developer Command Prompt)

```
cl /std:c++17 /EHsc /Fe:mimo.exe src\main.cpp src\compiler.cpp src\diagnostics.cpp src\util.cpp
```

### Option B: MinGW-w64 g++

```
g++ -std=c++17 -O2 -o mimo.exe src/main.cpp src/compiler.cpp src/diagnostics.cpp src/util.cpp
```

## Usage

```
./mimo.exe SHOW examples/hello.luke
./mimo.exe examples/inheritance_types.luke
```

Mimo writes the compiled JS next to your input (`.luke` → `.js`). With `SHOW`, it prints a friendly status and tries to run the output using Node if available.

## Roadmap

Prefer contributing to `vm/` for:

- Full tokenization and AST on the native path
- Contracts and static checks against Luke object layouts
- Rich expression parsing → bytecode
- Functions, blueprints, and GC-managed instances

JS emission here should shrink over time, not grow.
