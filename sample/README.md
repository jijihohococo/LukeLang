# sample/ — LukeLang landing page

## Status: **UNFINISHED / NON-PITCHABLE**

This folder is an honesty check: can LukeLang author a real landing page
**including fonts** without falling back to hand-written HTML/CSS/JS?

### What LukeLang owns here

| Concern | How |
| --- | --- |
| Document title | `NAME THE PAGE "LukeLang"` |
| Fonts | `BRING FONT "Syne" FROM "https://fonts.googleapis.com/..."` |
| CSS | `WEAR STYLE """ … """` |
| Markup | `FILL "root" WITH """ … """` |
| Click | `ASK jsOnClick WITH …` |

Source of truth: [`landing.luke`](landing.luke).

### What is still *not* LukeLang (why this is non-pitchable)

1. **WASM host glue** — `luke_browser_loader.js` is JavaScript. Luke cannot boot itself in a browser alone.
2. **HTML chassis** — `luke BUILD -target browser` still emits a tiny HTML shell (`#root` + script tags) from C++. Luke paints into `#root`; it does not emit the document.
3. **Remote font CDN** — `BRING FONT` injects a `<link stylesheet>`. There is no Luke-native font pack / `@font-face` file import from `luke_modules` yet.
4. **Remote hero image** — the hero photo is a URL string inside Luke CSS, not a Luke asset pipeline.
5. **Events are thin** — `jsOnClick` can set text on a target; no general Luke event/DOM model.

Until (1)–(2) are Luke-authored (or honestly framed as unavoidable runtime), **do not pitch LukeLang as “write the whole web in Luke.”**

### Build

```bash
cd vm
make
./build/luke BUILD ../sample/landing.luke -target browser -o ../sample/dist/landing
```

Open `sample/dist/landing.html` in a browser (needs network for fonts + hero image).

Headless smoke (Node — logs font/style calls, no layout):

```bash
node scripts/luke_browser_loader.cjs sample/dist/landing.wasm
```
