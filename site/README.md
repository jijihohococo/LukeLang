# lukelang.org

The official LukeLang site: a single static page, no build step, no framework.

```
site/
  index.html        landing page
  learn/            guided path, install → first program → Live Graph
  download/         requirements, platforms, optional dependencies, verification
  examples/         annotated tour of the acceptance suite
  community/        who builds it, how to contribute, the bar for a change
  news/             what shipped and when
  docs/             GENERATED — every document in the repository, hosted
  styles.css        palette, landing layout, shared nav/footer, motion
  pages.css         interior page system: masthead, sidebar, prose, rows
  main.js           scroll-driven motion only
  assets/           mark, wordmark, favicon (derived from assets/lukelang-logo.png)
```

Every page outside `docs/` is hand-written HTML sharing `styles.css`, `pages.css` and
`main.js`. The nav and footer are duplicated per page on purpose — nothing has to run before
deploying.

## Hosted documentation

`site/docs/` is generated from the Markdown in `docs/` and `documentations/papers/`, so the
website serves the documentation itself rather than linking out to GitHub. Regenerate after
editing any of that Markdown:

```bash
pip install markdown
python3 scripts/build_site_docs.py
```

The generator resolves cross-document links to hosted pages, falls back to the repository for
anything that is not a document, highlights `luke` and shell fences with the site's own token
classes, and builds the sidebar and per-page contents. CI fails if the committed output does
not match a fresh run.

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
