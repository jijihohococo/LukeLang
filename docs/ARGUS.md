# Argus — LukeLang Rendering Engine

> **Status:** v0.5 — widgets + modal focus + live regions + class/scroll/grid  
> **Name:** Argus  
> **Backend:** DOM presentment (not Skia)  
> **Layout:** [`Hanka`](./HANKA.md)  
> **Frontend track:** [`FRONTEND_ROADMAP.md`](./FRONTEND_ROADMAP.md) — **done**

## What Argus is

Thin **reactive patcher**: dirty node → surgical DOM update.

```text
Hanka / PLACE  →  Argus tree  →  paint  →  lukejs embedder  →  DOM
```

## a11y

- Default roles + `aria-label`
- `SLOT MODAL` → `argus_modal_open` on first mount (Tab trap, Escape restore); `CLEAR` → `argus_modal_close`
- `TRAP FOCUS IN` / `RESTORE FOCUS` / `ANNOUNCE` statements
- `SLOT TEXT … ANNOUNCE` / `ANNOUNCE URGENT` → `aria-live` polite/assertive (text updates announce automatically)

## Class hatch (Tailwind interop)

`WEAR "px-5 bg-indigo-600"` → `argus_js_class` → `class="…"`.  
Hanka owns layout via inline styles; Tailwind owns skin.  
`luke PUBLISH WEB app.luke --tailwind input.css` runs Tailwind JIT over `.luke` content when present.

## Breakpoints

- Container: `STACK BELOW` / `WRAP BELOW` (re-layout via `luke_viewport_relayout` on resize)
- Predicate: `WHEN THE VIEWPORT IS BELOW|ABOVE n`
- matchMedia: `AT LEAST` / `UNDER` / `BETWEEN`

## Related

- [`HANKA.md`](./HANKA.md) · [`FRONTEND_ROADMAP.md`](./FRONTEND_ROADMAP.md) · [`PRODUCTION_WEB.md`](./PRODUCTION_WEB.md)
