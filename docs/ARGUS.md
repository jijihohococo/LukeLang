# Argus — LukeLang Rendering Engine

> **Status:** v0.4 — widgets + a11y focus/live + motion beachhead  
> **Name:** Argus  
> **Backend:** DOM presentment (not Skia)  
> **Layout:** [`Hanka`](./HANKA.md) owns frames; `PLACE` still accepts explicit frames  
> **Frontend track:** [`FRONTEND_ROADMAP.md`](./FRONTEND_ROADMAP.md)

> **Direction (Path A, see [`STRATEGY.md`](./STRATEGY.md)):** Argus is a thin
> **reactive patcher** — dirty node → surgical DOM update. ROW/COLUMN paint via CSS flex
> (`argus_flex` / `argus_flow_frame` / parent attach); STACK/`PLACE` keep absolute frames.

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
| `INPUT` | text / email / password / checkbox / radio | `input` |
| `SELECT` | dropdown | `select` (options `a\|b\|c`) |
| `TABLE` | simple table | `table` (cells `h\|h;r\|r`) |
| `MODAL` | dialog surface | `div role=dialog` + focus trap |

Each node: `id`, frame `(x,y,w,h)`, `opacity`, optional `text` / `src`, a11y role/label, dirty flags.

## Luke surface

```luke
PLACE "cta" AS BUTTON AT 48, 700 SIZE 240, 48 SAY "Build something real"
PLACE "plan" AS SELECT AT 48, 760 SIZE 200, 40 SAY "Free|Pro|Team"
PLACE "grid" AS TABLE AT 48, 820 SIZE 400, 120 SAY "Name|Role;Ada|Builder"
PLACE "dlg" AS MODAL AT 200, 200 SIZE 320, 96 SAY "Saved"
SLOT INPUT AS CHECKBOX "agree" SIZE 24, 24 SAY "I agree"
PAINT THE SCREEN

TRAP FOCUS IN "dlg"
ANNOUNCE "Saved"
RESTORE FOCUS

SET THE OPACITY OF "cta" TO 0
FADE "cta" FROM 0 TO 1 OVER 300

SPEAK THE TEXT WIDTH OF "Hello"
SPEAK THE VIEWPORT WIDTH
SPEAK THE VIEWPORT HEIGHT
SPEAK THE CLOCK

WHEN THE VIEWPORT IS AT LEAST 800 WIDE DO
  SPEAK "wide"
END WHEN
```

## Runtime files

- `vm/runtime/argus.h` — scene tree + paint + fade + focus/announce imports  
- `lukejs` embedder: upsert/frame/text/image/input/a11y/select/table/measure_text/viewport_*/now_ms/argus_fade/focus_trap/announce  
- Demo: `argus_demo.luke`, `frontend_widgets.luke`, `frontend_wrap_forms.luke`

## a11y (beachhead)

Paint applies default roles (`button`, `textbox`, `checkbox`, `radio`, `img`, `listbox`, `table`, `dialog`)  
plus `aria-label` from placeholder/text when present.

- `TRAP FOCUS IN|ON "id"` — Tab cycle + Escape restore (`argus_focus_trap`)  
- `RESTORE FOCUS` / `RELEASE FOCUS` — pop previous active element  
- `ANNOUNCE "…"` — polite `aria-live` region (`__luke_live`)  
- `SLOT MODAL` paints with `role=dialog` and traps focus automatically

## Motion (beachhead)

- `SET THE OPACITY OF "id" TO n` — immediate  
- `FADE "id" [FROM a] TO b [OVER ms]` — ease-out cubic (browser rAF; native stepped)

## Breakpoints (beachhead)

Declarative matchMedia handlers (separate from resize `WHEN THE VIEWPORT CHANGES`):

- `WHEN THE VIEWPORT IS AT LEAST N [WIDE] DO` → `min:N`
- `WHEN THE VIEWPORT IS UNDER N DO` → `max:N`
- `WHEN THE VIEWPORT IS BETWEEN lo AND hi [WIDE] DO` → `min:lo:max:hi`

## Non-goals

- Owning layout math (that's **Hanka**)
- WebGL/Skia
- CSS-as-source-of-truth

## Related

A browser demo paints via `PLACE` / Hanka + `PAINT THE SCREEN` without `FILL "root" WITH """…html…"""`.

- Layout: [`HANKA.md`](./HANKA.md)  
- Frontend: [`FRONTEND_ROADMAP.md`](./FRONTEND_ROADMAP.md)  
- Production: [`PRODUCTION_WEB.md`](./PRODUCTION_WEB.md)  
- Change model: [`REACTIVE.md`](./REACTIVE.md) (Argus consumes paint invalidation)
