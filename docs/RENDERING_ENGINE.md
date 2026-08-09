# Luke Rendering Engine (`luke-render`)

> **Status:** Beachhead in progress  
> **Backend:** DOM presentment (not Skia)  
> **Layout:** explicit frames for now — see [`LAYOUT_ENGINE.md`](./LAYOUT_ENGINE.md) (future)

## Goal

Own a **Luke scene tree** with Luke terms.  
Browser DOM is only the paint host.  
Fast path: dirty nodes → patch DOM (no full `innerHTML` rebuild).

```text
PLACE / scene API  →  luke-render tree  →  paint  →  thin JS embedder  →  DOM
```

## Principles

1. **Not Skia** — no pixel raster engine in-core  
2. **Not browser layout** — frames come from Luke (explicit today; layout engine later)  
3. **Speed** — transform/opacity friendly; arena-friendly node storage  
4. **Pixel-aimed** — absolute frames + local fonts; DPR handling follows  
5. **Conversational surface** — `PLACE`, `PAINT THE SCREEN`

## Node kinds (v0)

| Kind | Role |
| --- | --- |
| `BOX` | colored / empty region |
| `TEXT` | label |
| `BUTTON` | clickable text host |
| `IMAGE` | full-bleed / framed image |

Each node: `id`, frame `(x,y,w,h)`, `opacity`, optional `text` / `src`, dirty flags.

## Luke surface (v0)

```luke
IMPORT std/render

PLACE "hero" AS IMAGE AT 0, 0 SIZE 1440, 900 FROM "https://…"
PLACE "brand" AS TEXT AT 48, 520 SIZE 900, 120 SAY "LukeLang"
PLACE "cta" AS BUTTON AT 48, 700 SIZE 240, 48 SAY "Build something real"
PAINT THE SCREEN

WHEN "cta" IS CLICKED DO
  PLACE "out" AS TEXT AT 48, 760 SIZE 900, 40 SAY "Still LukeLang."
  PAINT THE SCREEN
END WHEN
```

## Runtime files

- `vm/runtime/luke_render.h` — tree + paint scheduling  
- `lukejs` embedder ops: `render_upsert`, `render_frame`, `render_text`, `render_image`  
- `vm/stdlib/render.luke` — thin wrappers if needed

## Non-goals (v0)

- Flex/column layout (future layout engine)
- WebGL/Skia
- CSS-as-source-of-truth
- Full a11y tree mapping (button/text roles come next)

## Success metric

A browser demo paints via `PLACE` + `PAINT THE SCREEN` without `FILL "root" WITH """…html…"""`.
