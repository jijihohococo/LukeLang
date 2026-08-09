# Hanka — LukeLang Layout Engine

> **Status:** Started (v0 beachhead)  
> **Name:** Hanka  
> **Role:** Own layout numbers → feed Argus frames  
> **Not:** browser flex/grid as source of truth

## Pipeline

```text
Luke UI terms → Hanka (layout) → frames → Argus (paint) → DOM
```

## Principles

1. Luke owns `x, y, w, h`  
2. Explicit sizes in v0 (no intrinsic text measure yet)  
3. Containers: `COLUMN` / `ROW` / `STACK`  
4. Leaves become Argus nodes via `LAY OUT` then `PAINT`

## Luke surface (v0)

```luke
IMPORT std/hanka
IMPORT std/argus

BEGIN STACK AT 0, 0 SIZE 1280, 720
  SLOT IMAGE "hero" AT 0, 0 SIZE 1280, 720 FROM "https://…"
END STACK

BEGIN COLUMN AT 48, 420 SIZE 1184, 280 PAD 0 GAP 16
  SLOT TEXT "brand" SIZE 900, 80 SAY "LukeLang"
  SLOT TEXT "lead" SIZE 900, 48 SAY "Hanka lays out. Argus paints."
  SLOT BUTTON "cta" SIZE 220, 48 SAY "Build something real"
END COLUMN

LAY OUT THE SCREEN
PAINT THE SCREEN
```

| Term | Meaning |
| --- | --- |
| `BEGIN COLUMN\|ROW\|STACK AT x, y SIZE w, h [PAD n] [GAP n]` | Open a layout box |
| `SLOT TEXT\|BUTTON\|IMAGE\|BOX\|INPUT "id" [AT ox, oy] SIZE w, h …` | Add a leaf |
| `END COLUMN\|ROW\|STACK` | Close the open box |
| `LAY OUT THE SCREEN` | Resolve boxes → Argus frames |

`AT` on `SLOT` is for `STACK` (relative to the stack origin).  
`COLUMN` / `ROW` pack along their axis with `PAD` + `GAP`.

## Runtime

- `vm/runtime/hanka.h` — boxes, slots, layout → `argus_place_*`  
- `vm/stdlib/hanka.luke` — thin wrappers  
- Demo: `examples/build/hanka_demo.luke`

## v0 non-goals

- Intrinsic text measurement  
- Breakpoints / wrap  
- Nested containers (auto-close previous `BEGIN` for now)  
- CSS flex as authority

## Next (v1)

- Nested `COLUMN`/`ROW` trees  
- Align start/center/end  
- Measure text via embedder  
- Viewport / DPR snapping

## Related

- Paint: [`ARGUS.md`](./ARGUS.md)  
- History note: [`LAYOUT_ENGINE.md`](./LAYOUT_ENGINE.md)
