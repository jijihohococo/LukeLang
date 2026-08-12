# LukeLang Frontend Production Roadmap

> **Wedge:** reactive full-stack web — see [`STRATEGY.md`](./STRATEGY.md) (the decision record this roadmap serves).  
> **Current track:** Frontend (this document) + Backend beachhead opening  
> **Next track:** Backend deepen ([`BACKEND_ROADMAP.md`](./BACKEND_ROADMAP.md)) — unlocks full-stack reactivity  
> **Master checklist:** [`TaskList.md`](../TaskList.md)  
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
| 1 | **Frontend** *(now)* | Layout, widgets, a11y, publish, production proof | **done** |
| 2 | Backend | HTTP/server, DB, auth, full-stack **reactive** apps | **active beachhead** — [`BACKEND_ROADMAP.md`](./BACKEND_ROADMAP.md) |
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
| Per-axis ALIGN (main + cross) | ✅ | `ALIGN MAIN CENTER CROSS START` · `ALIGN CENTER, START` |
| Text measure / SIZE AUTO | ✅ | `SIZE AUTO,h` · `THE TEXT WIDTH OF` |
| Scroll container | ✅ | `BEGIN … SCROLL` |
| GRID opt-in | ✅ | `BEGIN GRID … COLUMNS n` (never mandatory) |
| Breakpoints / viewport | ✅ | `STACK BELOW` / `WRAP BELOW` · `WHEN … BELOW/ABOVE` · matchMedia `AT LEAST/UNDER/BETWEEN` · resize `CHANGES` |
| Widget: SELECT/dropdown | ✅ | `SLOT SELECT` · options `a\|b\|c` |
| Widget: TABLE | ✅ | `SLOT TABLE` · cells `h\|h;r\|r` |
| Widget: MODAL | ✅ | `SLOT MODAL` · focus trap on mount |
| Rich form controls | ✅ | email/password + select + checkbox/radio |
| a11y roles / labels | ✅ | `argus_a11y` defaults + aria-label |
| a11y focus / live regions | ✅ | `OPEN/CLOSE THE MODAL` · `SLOT … ANNOUNCE` · `TRAP FOCUS` / `ANNOUNCE` |
| Tailwind class hatch | ✅ | `WEAR "…"` · `PUBLISH WEB --tailwind` |
| Motion polish | ✅ | `SET THE OPACITY OF` · `FADE … OVER ms` (rAF + ease-out) |
| `luke PUBLISH WEB` | ✅ | alias of browser BUILD + ship checklist |
| Production stress app | ✅ | `frontend_stress.luke` (100) · `frontend_pressure.luke` (2500) |
| Benchmarks / CI matrix | 🟡 | native `ms=` in stress; CI matrix next |
| Live DevTools UI / time-travel | ✅ beachhead | client scrub (`live_graph_scrub.luke`); server-seq scrub still open |

---

## Surface cheat sheet

```luke
BEGIN ROW AT 0, 0 SIZE 600, 80 GAP 8 STACK BELOW 640
  SLOT BUTTON "buy" SIZE AUTO, 40 SAY "Buy" WEAR "px-5 rounded-xl bg-indigo-600 text-white"
END ROW

BEGIN GRID AT 0, 100 SIZE 400, 200 COLUMNS 2 GAP 8 SCROLL
  SLOT TEXT "status" SIZE AUTO, 24 SAY "Ready" ANNOUNCE
END GRID

WHEN THE VIEWPORT IS BELOW 640 DO
  SPEAK "narrow"
END WHEN

WHEN THE VIEWPORT IS AT LEAST 800 WIDE DO
  SPEAK "desktop"
END WHEN

luke PUBLISH WEB app.luke -o dist/app --tailwind input.css
```

**Coexistence:** Hanka owns *layout* (inline flex/grid/size). Tailwind `WEAR` owns *skin*
(color, type, border, shadow). Tailwind layout utilities (`flex`, `w-*`) lose to inline styles on purpose.

---

## Definition of done (Frontend track) — **met**

1. Per-axis align ✅  
2. Scroll ✅  
3. Grid opt-in ✅  
4. Declarative breakpoints ✅  
5. a11y focus-trap + live regions ✅  
6. TaskList/roadmap drift fixed ✅  
7. Tailwind class hatch ✅  

---

## Signature line

> **Lukelang owns the page.**
