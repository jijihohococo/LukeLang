# LukeLang Frontend Production Roadmap

> **Wedge:** reactive full-stack web — see [`STRATEGY.md`](./STRATEGY.md) (the decision record this roadmap serves).  
> **Current track:** Frontend (this document)  
> **Next track:** Backend (unlocks full-stack reactivity — likely the true signature)  
> **Parked:** Scripting → Mobile → Game, plus the own-the-pixels renderer (earned post-beachhead)

Reactive change model: [`REACTIVE.md`](./REACTIVE.md)  
Ship path: [`PRODUCTION_WEB.md`](./PRODUCTION_WEB.md)

> **Renderer direction (Path A):** compile to **DOM + CSS** — the browser lays out and paints;
> Argus is a thin **reactive patcher** (dirty node → surgical DOM update) and Hanka emits
> flex/grid rather than computing absolute frames. Rationale in [`STRATEGY.md`](./STRATEGY.md).

---

## Big picture (product tracks)

| Order | Track | Goal | Status |
|------:|-------|------|--------|
| 1 | **Frontend** *(now)* | Layout, widgets, a11y, publish, production proof | active |
| 2 | Backend | HTTP/server, DB, auth, full-stack **reactive** apps | next |
| 3 | Scripting | Tooling DX, packages, automation | parked |
| 4 | Mobile | Native shells / shared Luke UI | parked |
| 5 | Game | Entities, loops, rendering beyond DOM | parked |
| — | Own-the-pixels renderer (canvas/WebGL) | Flutter-style; only in *full* form | parked |

Parked ≠ cancelled: these are sequenced **after** the frontend proof point, picked up from
a position of strength. Do **not** pull parked tracks into Frontend PRs unless they unblock ship.

---

## Frontend gap status

| Gap | Status | Beachhead |
|-----|--------|-----------|
| Layout ALIGN START/CENTER/END | ✅ | `BEGIN ROW … ALIGN CENTER` |
| Text measure / SIZE AUTO | ✅ | `SIZE AUTO,h` · `THE TEXT WIDTH OF` |
| Breakpoints / viewport | ✅ | `THE VIEWPORT WIDTH/HEIGHT` · `WRAP` · `WHEN THE VIEWPORT CHANGES` |
| Widget: SELECT/dropdown | ✅ | `SLOT SELECT` · options `a\|b\|c` |
| Widget: TABLE | ✅ | `SLOT TABLE` · cells `h\|h;r\|r` |
| Widget: MODAL | ✅ | `SLOT MODAL` · `role=dialog` |
| Rich form controls | ✅ | email/password + select + checkbox/radio |
| a11y roles / labels | ✅ | `argus_a11y` defaults + aria-label |
| Motion polish | ✅ | `SET THE OPACITY OF` · `FADE … OVER ms` (rAF + ease-out) |
| `luke PUBLISH WEB` | ✅ | alias of browser BUILD + ship checklist |
| Production stress app | ✅ | `frontend_stress.luke` (100) · `frontend_pressure.luke` (2500) |
| Benchmarks / CI matrix | 🟡 | native `ms=` in stress; CI matrix next |
| Live DevTools UI / time-travel | ✅ beachhead | client scrub (`live_graph_scrub.luke`); server-seq scrub still open |

---

## Surface cheat sheet

```luke
BEGIN ROW AT 0, 0 SIZE 220, 120 PAD 4 GAP 8 WRAP
  SLOT BUTTON "a" SIZE 90, 36 SAY "One"
  SLOT BUTTON "b" SIZE 90, 36 SAY "Two"
  SLOT BUTTON "c" SIZE 90, 36 SAY "Three"
END ROW

SLOT INPUT AS CHECKBOX "agree" SIZE 24, 24 SAY "I agree"
SLOT INPUT AS RADIO "plan" SIZE 24, 24 SAY "Pro"

SET THE OPACITY OF "a" TO 0
FADE "a" FROM 0 TO 1 OVER 300

WHEN THE VIEWPORT CHANGES DO
  SPEAK THE VIEWPORT WIDTH
END WHEN

SPEAK THE VIEWPORT HEIGHT
SPEAK THE CLOCK

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
