# LukeLang Production Web Stack

> **Status:** v1 in progress (shippable static apps)  
> **Goal:** Ship real browser apps as **Luke-owned** UI + Build AOT, not a thin skin over React.

## Stack layers

```text
Luke app (conversational)
    ↓
Hanka          nested COLUMN / ROW / STACK
    ↓
Argus          TEXT / BUTTON / INPUT / IMAGE / BOX
    ↓
Events         CLICKED | CHANGED | SUBMITTED | FETCH READY
Routing        GO TO / WHEN THE ROUTE IS …
Data           FOR EACH list → UI · START FETCH (async)
    ↓
Thin boot      WASM + lukejs embedder (runtime, not app JS)
    ↓
Static dist    .html + .wasm + fonts/
```

## Checklist

| Capability | Now |
| --- | --- |
| Nested Hanka | yes |
| Inputs (+ email/password) | yes |
| `THE VALUE OF` / validation helpers | yes |
| Hash routing | yes |
| Async fetch GET/POST + status/body | yes |
| `FOR EACH` → UI | yes |
| Deploy folder | html + wasm + fonts |
| Motion / a11y polish | next |
| `luke PUBLISH WEB` | next |
| Reactive runtime | Phases 1–8 — see [`REACTIVE.md`](./REACTIVE.md) |

## Surface (v1)

```luke
BEGIN COLUMN AT 48, 48 SIZE 720, 520 PAD 0 GAP 16
  BEGIN ROW AT 0, 0 SIZE 720, 48 PAD 0 GAP 12
    SLOT BUTTON "nav-home" SIZE 120, 44 SAY "Home"
    SLOT BUTTON "nav-search" SIZE 120, 44 SAY "Search"
  END ROW
  SLOT INPUT "email" AS EMAIL SIZE 480, 48 SAY "you@example.com"
  SLOT INPUT "pass" AS PASSWORD SIZE 480, 48 SAY "Password"
END COLUMN
LAY OUT THE SCREEN
PAINT THE SCREEN

FOR EACH item IN items DO
  PLACE "row0" AS TEXT AT 48, 280 SIZE 640, 32 SAY item
END FOR

START FETCH "demo" GET "https://example.com/api"
WHEN FETCH "demo" IS READY DO
  SET body TO THE BODY OF FETCH "demo"
  SET st TO THE STATUS OF FETCH "demo"
END WHEN
```

## Demo / deploy

```bash
luke BUILD examples/build/web_app.luke -target browser -o dist/app
# ship: dist/app.html + dist/app.wasm (+ fonts/)
```

## Related

- [`ARGUS.md`](./ARGUS.md) · [`HANKA.md`](./HANKA.md) · [`BUILD_MODE.md`](./BUILD_MODE.md) · [`REACTIVE.md`](./REACTIVE.md)
