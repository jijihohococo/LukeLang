# LukeLang Production Web Stack

> **Status:** In progress (beachhead → shippable static web apps)  
> **Goal:** Ship real browser apps as **Luke-owned** UI + Build AOT, not a thin skin over React.

## Stack layers

```text
Luke app (conversational)
    ↓
Hanka          layout numbers (COLUMN / ROW / STACK)
    ↓
Argus          scene paint (TEXT / BUTTON / INPUT / IMAGE / BOX)
    ↓
Events         WHEN … IS CLICKED|CHANGED|SUBMITTED
Routing        GO TO / WHEN THE ROUTE IS …
    ↓
Thin boot      WASM + lukejs embedder (runtime, not app JS)
    ↓
Static dist    .html + .wasm + fonts/
```

## Production checklist

| Capability | v0 (now) | Next |
| --- | --- | --- |
| Page shell / fonts / CSS | yes | asset hash / cache headers |
| Layout (Hanka) | yes (flat boxes) | nested + measure |
| Paint (Argus) | yes | a11y roles, focus |
| Buttons / click | yes | keyboard |
| **Inputs / forms** | **yes** | password/email types, validation helpers |
| **Read field values** | **yes** (`THE VALUE OF`) | bind-to-local sugar |
| **Hash routing** | **yes** | history API / SSR |
| Fetch | sync GET | async + POST + status |
| Lists → UI | manual | `FOR EACH` → SLOT |
| Deploy | folder of html/wasm/fonts | `luke PUBLISH WEB` |
| Motion | CSS only | `std/motion` |
| App JS | none (boot inlined) | keep it that way |

## Luke surface (production beachhead)

```luke
IMPORT std/hanka
IMPORT std/argus

NAME THE PAGE "Luke App"
WEAR STYLE """ … """

THIS IS FUNCTION showHome DO
  CLEAR THE SCREEN
  BEGIN COLUMN AT 48, 48 SIZE 720, 400 PAD 0 GAP 16
    SLOT TEXT "title" SIZE 640, 56 SAY "Home"
    SLOT BUTTON "to-search" SIZE 180, 44 SAY "Search"
  END COLUMN
  LAY OUT THE SCREEN
  PAINT THE SCREEN
END FUNCTION

THIS IS FUNCTION showSearch DO
  CLEAR THE SCREEN
  BEGIN COLUMN AT 48, 48 SIZE 720, 400 PAD 0 GAP 16
    SLOT TEXT "title" SIZE 640, 56 SAY "Search"
    SLOT INPUT "q" SIZE 480, 44 SAY "Query…"
    SLOT BUTTON "go" SIZE 120, 44 SAY "Go"
    SLOT TEXT "out" SIZE 640, 40 SAY ""
  END COLUMN
  LAY OUT THE SCREEN
  PAINT THE SCREEN
END FUNCTION

GO TO "home"
ASK showHome

WHEN THE ROUTE IS "home" DO
  ASK showHome
END WHEN

WHEN THE ROUTE IS "search" DO
  ASK showSearch
END WHEN

WHEN "to-search" IS CLICKED DO
  GO TO "search"
END WHEN

WHEN "go" IS CLICKED DO
  MY NAME IS q AS TEXT
  SET q TO THE VALUE OF "q"
  PLACE "out" AS TEXT AT 48, 280 SIZE 640, 40 SAY q
  PAINT THE SCREEN
END WHEN
```

## Deploy artifact

```bash
luke BUILD examples/build/web_app.luke -target browser -o dist/app
# ship: dist/app.html + dist/app.wasm (+ fonts/)
```

No app-authored `.js`. Boot is runtime, inlined.

## Related

- [`ARGUS.md`](./ARGUS.md) — paint  
- [`HANKA.md`](./HANKA.md) — layout  
- [`BUILD_MODE.md`](./BUILD_MODE.md) — AOT / browser target
