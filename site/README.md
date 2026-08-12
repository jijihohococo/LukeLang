# lukelang.org (stub)

Static marketing surface for LukeLang. Ship this directory (or its build) to the
public host. Content points at in-repo docs and the wall deploy proof.

```bash
# local preview
python3 -m http.server 8080 --directory site
```

Production: put `site/` behind Caddy as `/` and `examples/deploy/wall/dist` as
the Live Graph demo under `/wall/` (see `examples/deploy/wall/Caddyfile`).
