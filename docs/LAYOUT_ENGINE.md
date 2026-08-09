# Layout Engine — FUTURE JOB

> **Status:** Not started. Parked on purpose.  
> **Do not build yet.** Rendering engine lands first (DOM presentment + Luke scene terms).  
> **Owner path:** after `luke-render` can mount/paint/dirty-update nodes.

## Intent

Luke owns **layout numbers** (x, y, w, h).  
Browser flex/grid is **not** the source of truth.

```text
Luke UI terms → Layout Engine (future) → frames → Rendering Engine (now) → DOM
```

Pixel-perfect + speed requires this split. Rendering can proceed with **explicit frames**
(`PLACE … AT … SIZE …`) until layout exists.

## Why later

- Rendering beachhead unblocks scene/DOM/`PAINT` without boiling the ocean
- Layout is its own product: constraints, wrap, measure text, DPR, breakpoints
- Wrong layout early would freeze bad APIs into Luke syntax

## Scope (when we start)

### v0
- `STACK` (absolute children)
- `COLUMN` / `ROW` with `PAD`, `GAP`, align start/center/end
- Explicit sizes; no intrinsic text measure yet (or measure via embedder once)

### v1
- Intrinsic text measurement (embedder font metrics)
- `AT WIDTH …` breakpoints
- Scroll containers / clip
- Safe-area / DPR snapping

### Non-goals
- Full CSS compatibility
- Skia/Impeller
- Browser layout as authority

## Proposed Luke terms (draft — not shipped)

```luke
COLUMN LEFT BOTTOM PAD 48 GAP 16 DO
  SAY BRAND "LukeLang"
  SAY LEAD "…"
  BUTTON "cta" LABEL "Build something real"
END
```

Layout emits frames; render paints them.

## Acceptance when revived

1. Same Luke UI tree lays out deterministically at a given viewport
2. No dependency on CSS flex for app layout
3. Frames feed existing `luke-render` without HTML string rebuilds
4. Docs + tests for Column/Row/Stack

## Related

- Current work: [`RENDERING_ENGINE.md`](./RENDERING_ENGINE.md)
- Doctrine: Build AOT + arena; speed first; DOM presentment (not Skia)
