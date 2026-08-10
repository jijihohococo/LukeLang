# Hanka — LukeLang Layout Engine

> **Status:** v1.1 — nested boxes + ALIGN + AUTO measure  
> **Name:** Hanka  
> **Role:** Own layout numbers → feed Argus frames  
> **Not:** browser flex/grid as source of truth  
> **Frontend track:** [`FRONTEND_ROADMAP.md`](./FRONTEND_ROADMAP.md)

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

BEGIN ROW AT 48, 48 SIZE 720, 48 PAD 0 GAP 12 ALIGN CENTER
  SLOT TEXT "brand" SIZE AUTO, 40 SAY "LukeLang"
  SLOT BUTTON "cta" SIZE 160, 44 SAY "Build"
END ROW

LAY OUT THE SCREEN
PAINT THE SCREEN
```

| Term | Meaning |
| --- | --- |
| `BEGIN COLUMN\|ROW\|STACK AT x, y SIZE w, h [PAD n] [GAP n] [ALIGN START\|CENTER\|END]` | Open a layout box |
| `SLOT TEXT\|BUTTON\|IMAGE\|BOX\|INPUT\|SELECT\|TABLE\|MODAL "id" … SIZE w, h` | Add a leaf (`AUTO` ok for text width) |
| `END COLUMN\|ROW\|STACK` | Close the open box |
| `LAY OUT THE SCREEN` | Resolve boxes → Argus frames |

`ALIGN` packs leftover main-axis space and cross-aligns children (start/center/end).  
`AT` on `SLOT` is for `STACK` (relative to the stack origin).

## Runtime

- `vm/runtime/hanka.h` — boxes, slots, layout → `argus_place_*`  
- `vm/stdlib/hanka.luke` — thin wrappers  
- Demos: `hanka_demo.luke`, `hanka_align.luke`, `frontend_widgets.luke`

## Still open

- Wrap / flex-grow  
- Breakpoint rebuild sugar  
- Align start/center/end **per-axis** (main vs cross independently)

## Related

- Paint: [`ARGUS.md`](./ARGUS.md)  
- Production: [`PRODUCTION_WEB.md`](./PRODUCTION_WEB.md)  
- History note: [`LAYOUT_ENGINE.md`](./LAYOUT_ENGINE.md)
