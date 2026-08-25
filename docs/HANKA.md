# Hanka — LukeLang Layout Engine

> **Status:** v1.4 — flex/grid + per-axis ALIGN + STACK/WRAP BELOW + SCROLL + WEAR  
> **Name:** Hanka  
> **Role:** Own layout numbers → feed Argus frames / CSS flex/grid  
> **Not:** browser flex/grid as source of truth for structure  
> **Frontend track:** [`FRONTEND_ROADMAP.md`](./FRONTEND_ROADMAP.md) — **done**

> **Direction (Path A):** ROW/COLUMN emit CSS **flex**; `BEGIN GRID` is an **opt-in** CSS grid
> container. STACK and explicit `PLACE` still use absolute frames.

## Pipeline

```text
Luke UI terms → Hanka (layout) → frames / flex / grid → Argus (paint) → DOM
```

## Luke surface

```luke
raw "BEGIN ROW AT 0, 0 SIZE 600, 80 GAP 8 STACK BELOW 640"
raw "SLOT BUTTON \"a\" SIZE 80, 40 SAY \"A\" WEAR \"px-5 bg-indigo-600\""
raw "END ROW"
raw "BEGIN GRID AT 0, 100 SIZE 400, 200 COLUMNS 2 GAP 8 SCROLL"
raw "SLOT TEXT \"status\" SIZE AUTO, 24 SAY \"Ready\" ANNOUNCE"
raw "END GRID"
raw "LAY OUT THE SCREEN"
raw "PAINT THE SCREEN"
```

| Term | Meaning |
| --- | --- |
| `BEGIN COLUMN\|ROW\|STACK\|GRID AT x, y SIZE w, h …` | Open a layout box |
| `ALIGN START\|CENTER\|END` | Same value on main and cross |
| `ALIGN MAIN … CROSS …` / `ALIGN main, cross` | Independent axes |
| `WRAP` / `WRAP BELOW n` | Wrap now, or when viewport &lt; n |
| `STACK BELOW n` | ROW becomes column when viewport &lt; n |
| `SCROLL` | `overflow:auto` scroll container |
| `COLUMNS n` / `COLS n` | GRID column count |
| `WEAR "classes"` | Tailwind (or any) class hatch — skin only |
| `SLOT … ANNOUNCE` / `ANNOUNCE URGENT` | aria-live polite / assertive |

## Runtime

- `vm/runtime/hanka.h`  
- Demos: `hanka_align.luke`, `frontend_done.luke`, `frontend_wrap_forms.luke`

## Still open

- Flex-grow / shrink polish  
- Container queries / height breakpoints

## Related

- Paint: [`ARGUS.md`](./ARGUS.md)  
- Production: [`PRODUCTION_WEB.md`](./PRODUCTION_WEB.md)
