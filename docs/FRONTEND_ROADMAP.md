# LukeLang Frontend Production Roadmap

> **Current track:** Frontend (this document)  
> **Later tracks:** Backend → Scripting → Mobile → Game

Luke owns the UI stack (Hanka → Argus → WASM boot).  
Reactive change model: [`REACTIVE.md`](./REACTIVE.md)  
Ship path: [`PRODUCTION_WEB.md`](./PRODUCTION_WEB.md)

---

## Big picture (product tracks)

| Order | Track | Goal |
|------:|-------|------|
| 1 | **Frontend** *(now)* | Layout, widgets, a11y, publish, production proof |
| 2 | Backend | HTTP/server, DB, auth, full-stack apps |
| 3 | Scripting | Tooling DX, packages, automation |
| 4 | Mobile | Native shells / shared Luke UI |
| 5 | Game | Entities, loops, rendering beyond DOM |

Do **not** pull later tracks into Frontend PRs unless they unblock ship.

---

## Frontend gap status

| Gap | Status | Beachhead |
|-----|--------|-----------|
| Layout ALIGN START/CENTER/END | ✅ | `BEGIN ROW … ALIGN CENTER` |
| Text measure / SIZE AUTO | ✅ | `SIZE AUTO,h` · `THE TEXT WIDTH OF` |
| Breakpoints / viewport | 🟡 | `THE VIEWPORT WIDTH` (resize events next) |
| Widget: SELECT/dropdown | ✅ | `SLOT SELECT` · options `a\|b\|c` |
| Widget: TABLE | ✅ | `SLOT TABLE` · cells `h\|h;r\|r` |
| Widget: MODAL | ✅ | `SLOT MODAL` · `role=dialog` |
| Rich form controls | 🟡 | email/password + select; checkbox/radio next |
| a11y roles / labels | ✅ | `argus_a11y` defaults + aria-label |
| Motion polish | 🟡 | opacity timelines exist; rAF/easing next |
| `luke PUBLISH WEB` | ✅ | alias of browser BUILD + ship checklist |
| Production stress app | 🟡 | `frontend_widgets.luke` beachhead |
| Benchmarks / CI matrix | ⬜ | next |
| Live DevTools UI / time-travel | ⬜ | after Reactive DevTools APIs merge |

---

## Surface cheat sheet

```luke
BEGIN ROW AT 0, 0 SIZE 640, 48 PAD 0 GAP 12 ALIGN CENTER
  SLOT TEXT "t" SIZE AUTO, 32 SAY "Hello"
  SLOT SELECT "plan" SIZE 200, 40 SAY "Free|Pro|Team"
  SLOT TABLE "g" SIZE 400, 120 SAY "A|B;1|2"
  SLOT MODAL "dlg" SIZE 320, 96 SAY "Saved"
END ROW

SPEAK THE VIEWPORT WIDTH
SPEAK THE TEXT WIDTH OF "Hello"

luke PUBLISH WEB app.luke -o dist/app
```

---

## Definition of done (Frontend track)

1. ALIGN + AUTO measure used in real demos  
2. Core widgets (modal/select/table) paint in browser  
3. a11y roles on interactive nodes  
4. `PUBLISH WEB` ships static dist  
5. One signature stress app + published benchmark baseline  
6. Docs list Frontend → Backend → Scripting → Mobile → Game order

---

## Signature line

> **Lukelang owns the page.**
