# sample/ — LukeLang landing page

## Status: pitchable web surface (gaps 1–4 closed)

| Gap | Fix |
| --- | --- |
| 1. App JS boot | Boot is **Luke runtime** (`vm/runtime/luke_browser_boot.js`), **inlined** into the HTML. Dist has no app-authored `.js`. |
| 2. C++ HTML chassis | Title, `@font-face`, CSS, and body come from Luke (`NAME THE PAGE` / `BRING FONT` / `WEAR STYLE` / `FILL`). Host only wraps doctype + runtime boot. |
| 3. CDN-only fonts | `BRING FONT "Syne" FROM "./fonts/syne-700.woff2"` copies packs + emits `@font-face`. |
| 4. Thin clicks | `WHEN "cta" IS CLICKED DO … END WHEN` exports a wasm handler and wires it from the runtime. |

Source of truth: [`landing.luke`](landing.luke) + [`fonts/`](fonts/).

### Build

```bash
cd vm
make
./build/luke BUILD ../sample/landing.luke -target browser -o ../sample/dist/landing
```

Open `sample/dist/landing.html` (artifacts: `.html`, `.wasm`, `fonts/*` — no app JS).

Headless smoke:

```bash
node scripts/luke_browser_loader.cjs sample/dist/landing.wasm
grep -q '@font-face' sample/dist/landing.html
grep -q 'luke_when_' sample/dist/landing.html
```
