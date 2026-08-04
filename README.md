# LukeLang — Programming with Personality

LukeLang is a human-friendly programming language that reads like conversation.
**The intended execution model is a native runtime** (own bytecode VM, heap, and GC) — not a JavaScript wrapper.

## Status

| Path | What it is | Use it? |
| --- | --- | --- |
| **`vm/`** — native Luke runtime | C++ bytecode VM with mark-sweep GC | **Yes — this is the real language direction** |
| `main.js` / `luke.js` | Legacy transpile-to-JavaScript path | Compatibility / reference only |
| `mimo/` | Older C++ front-end that still emitted JS | Legacy |

```bash
cd vm && make
./build/luke SHOW ../examples/native/hello.luke
```

See [`vm/README.md`](vm/README.md) for architecture, supported subset, and roadmap (functions, blueprints/objects on the Luke heap, retiring JS as the default engine).

---

## Legacy JS path (not the long-term runtime)

The material below documents the original conversational syntax. Much of the OOP surface still only exists on the JS transpile path today; it will be re-hosted on the native VM rather than kept as a JS wrapper.

### Quick Start (legacy)

- Install Node.js (v16+ recommended).
- Run a Luke file: `node luke.js SHOW examples/hello.luke`.
- Or transpile and run directly: `node main.js SHOW examples/oop_full.luke`.

## Design Principles

- Conversational keywords with friendly synonyms.
- Minimal punctuation; words over symbols.
- Clear OOP with private fields, inheritance, and unique, brand-specific keywords.
- Native execution first; optional JS emit only as interop later — not as the language itself.

## Core Syntax

- Comments: `// anything`.
- Program markers (optional): `LET'S START` / `GET OUTTA HERE`.
- Variables:
  - Declare: `MY NAME IS count SET TO 0` or `MY NAME IS count`.
  - Assign: `SET count TO 10`.
- Strings:
  - Wordy literal: `SPEAK Hello World`.
  - Quoted strings: `"Hello"`.
- Lists:
  - Literal: `MAKE LIST WITH 1, 2, 3`.
  - Indexing: `ITEM AT 2 OF nums`.
- Arithmetic and logic:
  - `ADD a AND b`, `SUBTRACT a AND b`.
  - `AND` (also used for string concatenation), `NOT`.

## Functions

Native VM supports these now (`examples/native/functions.luke`):

- Define: `THIS IS FUNCTION add WITH a, b DO` … `END FUNCTION`
- Call: `ASK add WITH 2, 3`
- Return: `GIVE BACK result`

Method-style `ASK obj TO method` still waits on blueprints.

## Classes (Blueprints)

Native VM supports these now:

- `BLUEPRINT Dog FOLLOWS Animal IMPLEMENTS Eater DO` … `END CLASS`
- `CONTRACT Eater DO` / `MUST METHOD eat WITH food`
- `HAS` / `PRIVATE` / `SECRET` fields; `ALWAYS HAS` statics
- `WHEN BORN` / `METHOD` / `ALWAYS METHOD` / `SECRET METHOD`
- `NEW`, `ASK obj TO method`, `SELF`, `CALL PARENT`
- Closures: nested `THIS IS FUNCTION` capturing outer locals

See `examples/native/oop.luke`, `advanced_oop.luke`, `closures.luke`, `privacy.luke`.

## Output

- `SPEAK`, `YELL`, `SHOUT`, `SAY`

## Native examples

```bash
cd vm && make test
```

## Roadmap (true language, not a wrapper)

- ~~Native functions with call frames~~
- ~~Blueprints / objects on Luke-owned layouts~~
- ~~Contracts, statics, privacy, closures~~
- Modules and a richer standard library
- Keep JS emit only as an optional interop backend
