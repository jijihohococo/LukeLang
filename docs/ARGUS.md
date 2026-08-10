# Argus — LukeLang Rendering Engine

> **Status:** v0.2 — widgets + a11y beachhead  
> **Name:** Argus  
> **Backend:** DOM presentment (not Skia)  
> **Layout:** [`Hanka`](./HANKA.md) owns frames; `PLACE` still accepts explicit frames  
> **Frontend track:** [`FRONTEND_ROADMAP.md`](./FRONTEND_ROADMAP.md)

## What Argus is

Argus owns the **Luke scene tree** and paints it.  
Browser DOM is only the host surface.  
Fast path: dirty nodes → patch DOM (no full `innerHTML` rebuild).

```text
Hanka / PLACE  →  Argus tree  →  paint  →  thin JS embedder  →  DOM
```

## Node kinds

| Kind | Role | DOM |
| --- | --- | --- |
| `BOX` | colored / empty region | `div` |
| `TEXT` | label | `div` |
| `BUTTON` | clickable | `button` |
| `IMAGE` | framed image | `div` + bg |
| `INPUT` | text field | `input` |
| `SELECT` | dropdown | `select` (options `a\|b\|c`) |
| `TABLE` | simple table | `table` (cells `h\|h;r\|r`) |
| `MODAL` | dialog surface | `div role=dialog` |

Each node: `id`, frame `(x,y,w,h)`, `opacity`, optional `text` / `src`, a11y role/label, dirty flags.

## Luke surface

```luke
PLACE "cta" AS BUTTON AT 48, 700 SIZE 240, 48 SAY "Build something real"
PLACE "plan" AS SELECT AT 48, 760 SIZE 200, 40 SAY "Free|Pro|Team"
PLACE "grid" AS TABLE AT 48, 820 SIZE 400, 120 SAY "Name|Role;Ada|Builder"
PLACE "dlg" AS MODAL AT 200, 200 SIZE 320, 96 SAY "Saved"
PAINT THE SCREEN

SPEAK THE TEXT WIDTH OF "Hello"
SPEAK THE VIEWPORT WIDTH
```

## Runtime files

- `vm/runtime/argus.h` — scene tree + paint  
- `lukejs` embedder: upsert/frame/text/image/input/a11y/select/table/measure_text/viewport_width  
- Demo: `argus_demo.luke`, `frontend_widgets.luke`

## a11y (beachhead)

Paint applies default roles (`button`, `textbox`, `img`, `listbox`, `table`, `dialog`)  
plus `aria-label` from placeholder/text when present.  
Full focus-trap / live regions still open.

## Non-goals

- Owning layout math (that's **Hanka**)
- WebGL/Skia
- CSS-as-source-of-truth

## Related

- Layout: [`HANKA.md`](./HANKA.md)  
- Production: [`PRODUCTION_WEB.md`](./PRODUCTION_WEB.md)
