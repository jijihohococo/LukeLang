# Hanka — LukeLang Layout Engine

> **Status:** v1.3 — nested boxes + per-axis ALIGN + WRAP + AUTO measure  
> **Name:** Hanka  
> **Role:** Own layout numbers → feed Argus frames  
> **Not:** browser flex/grid as source of truth  
> **Frontend track:** [`FRONTEND_ROADMAP.md`](./FRONTEND_ROADMAP.md)

> **Direction (Path A, see [`STRATEGY.md`](./STRATEGY.md)):** ROW/COLUMN now emit CSS
> **flex** containers (`argus_flex` / flow children); the browser resolves layout. STACK and
> explicit `PLACE` still use absolute frames. Full flex/grid coverage is still expanding.

## Pipeline

```text
Luke UI terms → Hanka (layout) → frames → Argus (paint) → DOM
```

## Principles

1. Luke owns `x, y, w, h`  
2. Explicit sizes by default; `AUTO` measures text via embedder  
3. Containers: `COLUMN` / `ROW` / `STACK`  
4. Leaves become Argus nodes via `LAY OUT` then `PAINT`

## Luke surface

```luke
IMPORT std/hanka
IMPORT std/argus

BEGIN ROW AT 48, 48 SIZE 220, 120 PAD 0 GAP 12 ALIGN START WRAP
  SLOT TEXT "brand" SIZE AUTO, 40 SAY "LukeLang"
  SLOT BUTTON "cta" SIZE 160, 44 SAY "Build"
  SLOT BUTTON "more" SIZE 120, 44 SAY "Docs"
END ROW

BEGIN ROW AT 48, 200 SIZE 400, 80 PAD 0 GAP 10 ALIGN MAIN CENTER CROSS START
  SLOT BUTTON "a" SIZE 40, 40 SAY "A"
  SLOT BUTTON "b" SIZE 40, 40 SAY "B"
END ROW

LAY OUT THE SCREEN
PAINT THE SCREEN
```

| Term | Meaning |
| --- | --- |
| `BEGIN COLUMN\|ROW\|STACK AT x, y SIZE w, h [PAD n] [GAP n] [ALIGN …] [WRAP]` | Open a layout box |
| `ALIGN START\|CENTER\|END` | Same value on main and cross axes |
| `ALIGN MAIN … CROSS …` or `ALIGN main, cross` | Independent main / cross packing |
| `SLOT TEXT\|BUTTON\|IMAGE\|BOX\|INPUT\|SELECT\|TABLE\|MODAL "id" … SIZE w, h` | Add a leaf (`AUTO` ok for text width) |
| `END COLUMN\|ROW\|STACK` | Close the open box |
| `LAY OUT THE SCREEN` | Resolve boxes → Argus frames |

`ALIGN` packs leftover main-axis space (`justify-content`) and independently cross-aligns children (`align-items`).  
Single-token `ALIGN CENTER` still sets both axes.  
`WRAP` flows children onto the next cross line/column when the main axis fills.  
`AT` on `SLOT` is for `STACK` (relative to the stack origin).

## Runtime

- `vm/runtime/hanka.h` — boxes, slots, layout → `argus_place_*`  
- `vm/stdlib/hanka.luke` — thin wrappers  
- Demos: `hanka_demo.luke`, `hanka_align.luke`, `frontend_wrap_forms.luke`, `frontend_widgets.luke`

## Still open

- Flex-grow / shrink  
- Container queries / height breakpoints

## Related

- Paint: [`ARGUS.md`](./ARGUS.md)  
- Production: [`PRODUCTION_WEB.md`](./PRODUCTION_WEB.md)  
- History note: [`LAYOUT_ENGINE.md`](./LAYOUT_ENGINE.md)  
- Change model: [`REACTIVE.md`](./REACTIVE.md) (Hanka consumes layout invalidation)
