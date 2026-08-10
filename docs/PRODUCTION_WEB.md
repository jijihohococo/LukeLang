# LukeLang Production Web Stack

> **Status:** v1 in progress (shippable static apps)  
> **Goal:** Ship real browser apps as **Luke-owned** UI + Build AOT, not a thin skin over React.  
> **Frontend track:** [`FRONTEND_ROADMAP.md`](./FRONTEND_ROADMAP.md)

## Stack layers

```text
Luke app (conversational)
    ↓
Hanka          nested COLUMN / ROW / STACK (+ ALIGN, AUTO)
    ↓
Argus          TEXT / BUTTON / INPUT / IMAGE / BOX / SELECT / TABLE / MODAL
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
| Nested Hanka | yes (+ `ALIGN`) |
| Inputs (+ email/password) | yes |
| Select / table / modal | yes (beachhead) |
| `THE VALUE OF` / validation helpers | yes |
| Text measure / `SIZE AUTO` | yes |
| Viewport width | yes |
| Hash routing | yes |
| Async fetch GET/POST + status/body | yes |
| `FOR EACH` → UI | yes |
| Deploy folder | html + wasm + fonts |
| a11y roles on paint | yes (beachhead) |
| Motion polish | in progress |
| `luke PUBLISH WEB` | yes |

## Surface (v1)

```luke
BEGIN COLUMN AT 48, 48 SIZE 720, 520 PAD 0 GAP 16 ALIGN CENTER
  BEGIN ROW AT 0, 0 SIZE 720, 48 PAD 0 GAP 12
    SLOT BUTTON "nav-home" SIZE 120, 44 SAY "Home"
    SLOT BUTTON "nav-search" SIZE 120, 44 SAY "Search"
  END ROW
  SLOT INPUT "email" AS EMAIL SIZE 480, 48 SAY "you@example.com"
  SLOT INPUT "pass" AS PASSWORD SIZE 480, 48 SAY "Password"
  SLOT SELECT "plan" SIZE 240, 40 SAY "Free|Pro|Team"
END COLUMN
LAY OUT THE SCREEN
PAINT THE SCREEN
```

## Demo / deploy

```bash
luke BUILD examples/build/web_app.luke -target browser -o dist/app
# or:
luke PUBLISH WEB examples/build/web_app.luke -o dist/app
# ship: dist/app.html + dist/app.wasm (+ fonts/)
```

## Related

- [`ARGUS.md`](./ARGUS.md) · [`HANKA.md`](./HANKA.md) · [`BUILD_MODE.md`](./BUILD_MODE.md) · [`FRONTEND_ROADMAP.md`](./FRONTEND_ROADMAP.md)
