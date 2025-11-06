# LukeLang — Programming with Personality

LukeLang is a human-friendly programming language that reads like conversation and compiles to modern JavaScript. Its goal: be easier than Python while feeling expressive and fun, yet powerful enough for real-world programming.

This README serves as the authoritative language guide and syntax reference.

## Quick Start

- Install Node.js (v16+ recommended).
- Run a Luke file: `node luke.js SHOW examples/hello.luke`.
- Or transpile and run directly: `node main.js SHOW examples/hello.luke`.

## Design Principles

- Conversational keywords with friendly synonyms.
- Minimal punctuation; words over symbols.
- Clear OOP with private fields, inheritance, and unique, brand-specific keywords.
- Seamless interop: compiles 1:1 to readable JavaScript.

## Core Syntax

- Comments: `// anything` (regular JS comments work in Luke files too).
- Program markers (optional): `LET'S START` / `GET OUTTA HERE`.
- Variables:
  - Declare: `MY NAME IS count SET TO 0` or `MY NAME IS count`.
  - Assign: `SET count TO 10`.
- Strings:
  - Wordy literal: `SPEAK Hello World` becomes `console.log("Hello World")` automatically.
  - Quoted block:
    ```
    QUOTE
      multi line
    END QUOTE
    ```
- Lists:
  - Literal: `MAKE LIST WITH 1, 2, 3` → `[1, 2, 3]`.
  - Indexing: `ITEM AT 2 OF nums` → `nums[2]`.
  - Property access: `OF` or dot: `OF obj GET name` → `obj.name`.
- Arithmetic and logic:
  - `ADD a AND b` → `a + b`, `SUBTRACT a AND b` → `a - b`.
  - `AND`, `OR`, `NOT` map to `&&`, `||`, `!`.

## Functions

- Define:
  - `THIS IS FUNCTION add WITH a, b DO` … `END FUNCTION`.
  - Synonyms: `MAKE FUNCTION`, `RECIPE`.
- Call:
  - Conversational: `ASK calculator TO add WITH 2, 3`.
  - Dot: `calculator.add(2, 3)`.
- Return:
  - `GIVE BACK result` (also `SEND BACK`, `HAND BACK`).

## Classes (Blueprints)

- Define:
  - `THIS IS CLASS Animal DO` … `END CLASS`.
  - Synonyms: `MAKE CLASS`, `BLUEPRINT`, `CLASS`.
- Inheritance:
  - `FROM`, `EXTENDS`, `:` and the brand alias `FOLLOWS`.
  - Example: `BLUEPRINT Dog FOLLOWS Animal DO`.
- Constructor:
  - `WHEN BORN WITH name DO` … `END BORN`.
  - Synonyms: `BORN`, `MAKE ONE`, `INIT`, `CONSTRUCTOR`, `BEGIN`, `CREATE`.
  - `SELF` maps to `this`.
  - If a class `FOLLOWS` a parent, a `super(...)` call is automatically inserted with inferred args; you can also call it manually (see below).

### Properties

- Private fields (true JS `#` fields):
  - `PRIVATE secretName` or `SECRET secretName` → `#secretName;`.
  - Initialized: `HAS age SET TO 10`.
  - Bare declaration: `HAS sound`.

### Methods

- Public method:
  - `METHOD speak WITH text DO` … `END METHOD`.
  - Synonyms: `ACTION`, `TRICK`, `ABILITY`.
- Private method (brand):
  - `PRIVATE METHOD whisper WITH msg DO` … `END METHOD`.
  - Also: `SECRET METHOD ...`. Compiles to `#whisper(...)` and calls within class become `SELF.#whisper(...)` automatically.
- Call parent method inside a method (brand):
  - `CALL PARENT speak WITH text` or `CALL SUPER speak WITH text`.
  - Compiles to `super.speak(text)`.

### Static Members (brand keywords)

- Static field:
  - `ALWAYS HAS population SET TO 0` (also `ETERNAL`, `FOREVER`).
  - Compiles to `static population = 0;`.
- Static method:
  - `ALWAYS METHOD count WITH a DO` … `END METHOD`.
  - Compiles to `static count(a) { ... }`.

### Interfaces (Contracts)

- Define a contract:
  ```
  CONTRACT Walkable DO
    MUST METHOD walk WITH speed
  END CONTRACT
  ```
- Implement contracts in a class:
  - `BLUEPRINT Robot FOLLOWS Machine IMPLEMENTS Walkable DO`.
  - Synonyms for `IMPLEMENTS`: `COMPLIES WITH`, `AGREES TO`, `ADHERES TO`.
- Compliance checks:
  - The transpiler emits a static check that throws an error if required methods are missing.

## Object Creation

- `NEW`, `CREATE`, `MAKE OBJECT` → `new`.
- With arguments: `NEW Dog WITH "Rex"` → `new Dog("Rex")`.

## Method Calls (Dual Syntax)

- Conversational:
  - `ASK buddy TO speak WITH "Hi"` → `buddy.speak("Hi")`.
  - `DO speak OF buddy WITH "Hi"` → `buddy.speak("Hi")`.
- Dot notation:
  - `buddy.speak("Hi")` (direct dot calls are preserved).

## Output

- `SPEAK message`, `YELL message`, `SHOUT message`, `SAY message` → `console.log(message)`.
- Wordy lines without punctuation are auto-quoted in `SPEAK`.

## Brand Vocabulary (uniqueness)

- `BLUEPRINT` for class, `WHEN BORN` for constructor.
- `ALWAYS`/`ETERNAL`/`FOREVER` for static members.
- `SECRET` for private, `CALL PARENT` for super.
- `CONTRACT` with `MUST METHOD` for interfaces.
- `FOLLOWS` as a friendly alias for inheritance.

## Examples

```luke
BLUEPRINT Animal DO
  PRIVATE secretName
  HAS sound SET TO "..."
  WHEN BORN WITH name DO
    SET SECRET OF SELF TO name
  END BORN
  METHOD speak DO
    SPEAK sound OF SELF
  END METHOD
END CLASS

BLUEPRINT Dog FOLLOWS Animal IMPLEMENTS Walkable DO
  HAS sound SET TO "Woof!"
  PRIVATE METHOD whisper WITH msg DO
    SPEAK "(" AND msg AND ")"
  END METHOD
  METHOD speak DO
    CALL PARENT speak
    DO whisper OF SELF WITH "Good boy"
  END METHOD
END CLASS

CONTRACT Walkable DO
  MUST METHOD walk WITH speed
END CONTRACT

SET buddy TO NEW Dog WITH "Rex"
ASK buddy TO speak
```

## Notes and Limitations

- Protected members are surfaced as `_member` names for now (advisory convention).
- Abstract classes/methods are not yet enforced; see roadmap.
- Module system is currently Node.js-based; use multiple `.luke` files and standard JS imports in the generated code as needed.

## Roadmap (toward “full” OOP and beyond)

- `ABSTRACT BLUEPRINT` and `ABSTRACT METHOD` with compile-time enforcement.
- `SHIELDED` (protected) with better runtime checking for subclass access.
- Traits/Mixins: `MIXIN` blocks that merge methods into classes.
- Module system with `IMPORT`/`EXPORT` brand words.
- Optional static typing layer with friendly type terms.

## Ease-of-Use Assessment

LukeLang favors readable words, minimizes punctuation, and provides conversational method calls. With the brand vocabulary (e.g., `FOLLOWS`, `ALWAYS`, `CONTRACT`), it stays distinct yet intuitive. It is simpler than Python for beginners because:

- Clear, word-first syntax for common tasks.
- Dual method-call styles reduce cognitive load.
- Automatic `super(...)` insertion reduces boilerplate.
- True private fields via JS `#` semantics.

Power-wise, LukeLang now supports private fields, inheritance, static members, interfaces with compliance checks, and parent method calls—providing practical “full OOP” capabilities for everyday development.