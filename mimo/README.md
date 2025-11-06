# Mimo Compiler (C++)

Mimo is the native C++ compiler for LukeLang. It compiles `.luke` files into JavaScript with a friendly, expressive personality. When something goes wrong, Mimo explains the exact error and how to fix it — like a helpful teammate.

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

## Personality & Errors

When Mimo encounters a problem, it speaks in plain language, points to the line/column, and suggests fixes. Example:

```
💥 Oops! Mimo stumbled on line 12, column 7.
I expected `DO` after `METHOD fly WITH height`.
Try: `METHOD fly WITH height DO` then end with `END METHOD`.
```

## Roadmap

- Full tokenization and AST
- Contracts and static checks
- Rich expression parsing
- Emit source maps