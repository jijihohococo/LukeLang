# Argus — LukeLang Rendering Engine

> **Status:** Started (v0 beachhead)  
> **Name:** Argus  
> **Backend:** DOM presentment (not Skia)  
> **Layout:** [`Hanka`](./HANKA.md) owns frames; `PLACE` still accepts explicit frames

## What Argus is

Argus owns the **Luke scene tree** and paints it.  
Browser DOM is only the host surface.  
Fast path: dirty nodes → patch DOM (no full `innerHTML` rebuild).

```text
Hanka / PLACE  →  Argus tree  →  paint  →  thin JS embedder  →  DOM
```

## Principles

1. **Not Skia** — no pixel raster engine in-core  
2. **Not browser layout** — frames come from Luke / Hanka (not CSS flex)  
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
| `INPUT` | text field (`SAY` = placeholder; read with `THE VALUE OF`) |

Each node: `id`, frame `(x,y,w,h)`, `opacity`, optional `text` / `src`, dirty flags.

## Luke surface (v0)

```luke
IMPORT std/argus

PLACE "hero" AS IMAGE AT 0, 0 SIZE 1440, 900 FROM "https://…"
PLACE "brand" AS TEXT AT 48, 520 SIZE 900, 120 SAY "LukeLang"
PLACE "cta" AS BUTTON AT 48, 700 SIZE 240, 48 SAY "Build something real"
PAINT THE SCREEN

WHEN "cta" IS CLICKED DO
  PLACE "out" AS TEXT AT 48, 760 SIZE 900, 40 SAY "Still LukeLang."
  PAINT THE SCREEN
END WHEN
```

`IMPORT std/render` remains a thin alias; prefer `std/argus`.

## Runtime files

- `vm/runtime/argus.h` — scene tree + paint  
- `lukejs` embedder ops: `argus_upsert`, `argus_frame`, `argus_text`, `argus_image`, `argus_clear`  
- `vm/stdlib/argus.luke` — function wrappers  
- Demo: `examples/build/argus_demo.luke`

## Non-goals (v0)

- Owning layout math (that's **Hanka**)
- WebGL/Skia
- CSS-as-source-of-truth
- Full a11y tree mapping beyond labels/roles/focus rings

## Success metric

A browser demo paints via `PLACE` / Hanka + `PAINT THE SCREEN` without `FILL "root" WITH """…html…"""`.

Production path: [`PRODUCTION_WEB.md`](./PRODUCTION_WEB.md).
