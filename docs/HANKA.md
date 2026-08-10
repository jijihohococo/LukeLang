# Hanka — LukeLang Layout Engine

> **Status:** v1.2 — nested boxes + ALIGN + WRAP + AUTO measure  
> **Name:** Hanka  
> **Role:** Own layout numbers → feed Argus frames  
> **Not:** browser flex/grid as source of truth  
> **Frontend track:** [`FRONTEND_ROADMAP.md`](./FRONTEND_ROADMAP.md)

> **Direction (Path A, see [`STRATEGY.md`](./STRATEGY.md)):** long-term, Hanka emits CSS
> **flex/grid** and lets the browser resolve layout, rather than computing absolute `x,y,w,h`
> frames itself. The frame-computing model below is the *current* implementation, not the destination.

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

LAY OUT THE SCREEN
PAINT THE SCREEN
```

| Term | Meaning |
| --- | --- |
| `BEGIN COLUMN\|ROW\|STACK AT x, y SIZE w, h [PAD n] [GAP n] [ALIGN START\|CENTER\|END] [WRAP]` | Open a layout box |
| `SLOT TEXT\|BUTTON\|IMAGE\|BOX\|INPUT\|SELECT\|TABLE\|MODAL "id" … SIZE w, h` | Add a leaf (`AUTO` ok for text width) |
| `END COLUMN\|ROW\|STACK` | Close the open box |
| `LAY OUT THE SCREEN` | Resolve boxes → Argus frames |

`ALIGN` packs leftover main-axis space and cross-aligns children (start/center/end) when not wrapping.  
`WRAP` flows children onto the next cross line/column when the main axis fills.  
`AT` on `SLOT` is for `STACK` (relative to the stack origin).

## Runtime

- `vm/runtime/hanka.h` — boxes, slots, layout → `argus_place_*`  
- `vm/stdlib/hanka.luke` — thin wrappers  
- Demos: `hanka_demo.luke`, `hanka_align.luke`, `frontend_wrap_forms.luke`, `frontend_widgets.luke`

## Still open

- Flex-grow / shrink  
- Align start/center/end **per-axis** (main vs cross independently)  
- Breakpoint rebuild sugar (`WHEN THE VIEWPORT CHANGES` + re-`LAY OUT` patterns)

## Related

- Paint: [`ARGUS.md`](./ARGUS.md)  
- Production: [`PRODUCTION_WEB.md`](./PRODUCTION_WEB.md)  
- History note: [`LAYOUT_ENGINE.md`](./LAYOUT_ENGINE.md)  
- Change model: [`REACTIVE.md`](./REACTIVE.md) (Hanka consumes layout invalidation)
