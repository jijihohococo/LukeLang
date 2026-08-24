# LukeLang Standard Library

This document outlines the standard library provided by LukeLang, including functions for input/output, file system operations, and other utilities.

## 1. Input/Output

LukeLang provides simple keywords for handling console input and output.

### `SPEAK`

The `SPEAK` keyword (and its synonyms `YELL`, `SHOUT`, `SAY`) prints a message to the console.

```luke
print("Hello, World!")
raw "LET name BE \"Luke\""
print("Hello, " + name)
```

### `ASK USER`

The `ASK USER` keyword prompts the user for input and assigns it to a variable.

```luke
let name = USER FOR "What is your name? "()
raw "name = ASK USER FOR \"What is your name? \""
```

## 2. File System — Build `IMPORT std/files`

File ops ship in Build via `std/files` (native; arena-backed TEXT):

```luke
import std/files
let body = readFile("hello.txt")
writeFile("out.txt", "hello from Luke")
```

See `vm/stdlib/files.luke` and `examples/build/modules.luke`.
Play/VM scripting file APIs remain a separate track.

## 3. Utility Functions

### `RANDOM`

Returns a random number between 0 and 1.

```luke
raw "LET randomNumber BE RANDOM"
print("Your lucky number is: " + randomNumber)
```

### `ROUND`

Rounds a number to the nearest integer.

```luke
raw "LET num BE 3.14"
print(ROUND(num) // Outputs: 3)
```

This standard library is continuously evolving. Prefer Build `vm/stdlib/*.luke` and `make test` over stale prose — see [`BUILD_MODE.md`](./BUILD_MODE.md).
