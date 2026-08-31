# lukelang.org

The official LukeLang site: a single static page, no build step, no framework.

```
site/
  index.html      markup + code samples
  styles.css      layout, palette, motion
  main.js         scroll-driven motion only
  assets/         mark, wordmark, favicon (derived from assets/lukelang-logo.png)
```

```bash
# local preview
python3 -m http.server 8080 --directory site
```

## Design rules

- **Palette** — black, white, green, golden, yellow. Nothing else.
- **Type** — Plus Jakarta Sans for everything; a system monospace stack for code only.
- **Editorial, not carded** — hairline rules, wide type and asymmetric grids carry the
  hierarchy. No boxed cards, no drop-shadowed panels.
- **Motion is scroll-driven** — reveals, path draw, count-ups, marquee. Nothing follows or
  restyles the pointer, and there is no custom cursor.
- **Degrades cleanly** — the hidden-then-revealed state is gated behind a `.js` class, so the
  page is fully readable without JavaScript. `prefers-reduced-motion` disables the motion.

## Assets

`assets/` is generated from the master logo at the repository root. The mark and wordmark are
split so the mark can sit on dark sections and the wordmark keeps its black-and-yellow identity;
`luke-wordmark-light.webp` is the dark-background variant.

## Deploy

Ship `site/` as the document root. Behind Caddy, serve it at `/` and the Live Graph demo
(`examples/deploy/wall/dist`) at `/wall/` — see `examples/deploy/wall/Caddyfile`.
