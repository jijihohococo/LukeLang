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
  status/           standalone page for status.lukelang.org
  styles.css        palette, landing layout, shared nav/footer, motion
  pages.css         interior page system: masthead, sidebar, prose, rows
  main.js           scroll-driven motion only
  assets/           mark, wordmark, favicon (derived from assets/lukelang-logo.png)
```

Every page outside `docs/` is hand-written HTML sharing `styles.css`, `pages.css` and
`main.js`. The nav and footer are duplicated per page on purpose — nothing has to run before
deploying.

## Search metadata

`scripts/build_site_meta.py` owns everything a crawler reads, for every page at once:

```bash
python3 scripts/build_site_meta.py
```

It rewrites the block between `<!-- seo:begin -->` and `<!-- seo:end -->` in each page —
canonical URL, robots directives, Open Graph and Twitter cards pointing at
`assets/og.png`, and JSON-LD (`WebSite` + `Person` everywhere, `SoftwareSourceCode` on the
landing page, `TechArticle` + `BreadcrumbList` on documents) — then regenerates
`sitemap.xml` and `robots.txt`. `lastmod` comes from the HEAD commit date so the sitemap does
not churn on every build.

The status page is skipped: it lives on another origin and stays out of the index.

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

## Status page

`site/status/` is deployed on its own subdomain at `status.lukelang.org`, so it is deliberately
self-contained: inline styles, its own favicon, and absolute links back to the main site. It
carries no shared CSS, which means an outage on the main host cannot take the status page's
styling with it.

Component state is data-driven: each row in the components list carries
`data-state="up|degraded|down|maintenance"`, and the uptime strip is built in a few lines of
JavaScript from an array. Point a checker at it — rewriting those attributes, or regenerating
the file — when real monitoring exists. The values committed here are placeholders.

## Deploy

```bash
scripts/deploy_site.sh                 # regenerate docs, upload, swap, verify
HOST=root@1.2.3.4 scripts/deploy_site.sh
```

The script uploads into a staging directory and swaps it in, so a failed transfer never leaves
a half-written document root. The previous release stays at `/var/www/lukelang.prev`:

```bash
ssh $HOST 'rm -rf /var/www/lukelang && mv /var/www/lukelang.prev /var/www/lukelang && systemctl reload nginx'
```

### Live host

`site/` is served from `/var/www/lukelang` by nginx (`/etc/nginx/sites-available/lukelang.conf`)
on the LukeLang VPS. Two hosts share one document root:

| Host | Root |
| --- | --- |
| `lukelang.org` | `/var/www/lukelang` |
| `www.lukelang.org` | 301 to the apex |
| `status.lukelang.org` | `/var/www/lukelang/status` |

TLS is issued by `/root/lukelang-tls.sh` on the server, which refuses to run until every name
resolves to that host and then calls certbot for all three at once.

## Getting indexed

`robots.txt` and `sitemap.xml` are generated, and an IndexNow key file sits at the document
root, so Bing, Yandex and Seznam can be told about a change directly:

```bash
scripts/indexnow_submit.py            # submit every URL in the sitemap
```

Google has no equivalent open endpoint. Add `https://lukelang.org` as a property in Google
Search Console, verify it (the DNS TXT method is easiest, since the domain is already on
Hostinger DNS), then submit `https://lukelang.org/sitemap.xml` once. Indexing takes a few days
from there.
