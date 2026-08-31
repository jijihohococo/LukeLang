#!/usr/bin/env python3
"""Render the in-repo Markdown documentation into hosted pages under site/docs/.

The site is deployed as plain static files, so the HTML this produces is
committed. Re-run after editing anything under docs/ or documentations/:

    python3 scripts/build_site_docs.py

Requires Python-Markdown (`pip install markdown`). Nothing at runtime.
"""

from __future__ import annotations

import html
import os
import re
import sys
from dataclasses import dataclass, field

try:
    import markdown
except ImportError:  # pragma: no cover - developer convenience
    sys.exit("build_site_docs: pip install markdown")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "site", "docs")
REPO = "https://github.com/lucasdmarshall/LukeLang"
STATUS = "https://status.lukelang.org"

# ── Which documents go where in the sidebar ─────────────────────────────

GROUPS: list[tuple[str, list[str]]] = [
    ("Start here", [
        "getting_started", "BUILD_MODE", "SYNTAX_V2_SPEC", "STRATEGY",
    ]),
    ("The language", [
        "language_reference", "standard_library", "INTEGER", "AST",
        "SYNTAX_V2_PLAN", "advanced_topics",
    ]),
    ("Reactive engine", [
        "REACTIVE", "REACTIVE_SPEC", "REACTIVE_ROADMAP",
    ]),
    ("Live Graph", [
        "LIVE_GRAPH", "DEPLOY",
    ]),
    ("Backend", [
        "BACKEND_ROADMAP", "AUTH", "NETWORK_DB_ROADMAP", "BACKEND_BENCHMARKS",
        "BACKEND_PUBLISH",
    ]),
    ("Frontend", [
        "FRONTEND_ROADMAP", "ARGUS", "HANKA", "LAYOUT_ENGINE",
        "RENDERING_ENGINE", "PRODUCTION_WEB",
    ]),
    ("Tooling & compiler", [
        "EDITOR_TOOLING", "contributor_guide", "BENCHMARKS", "SCORECARD", "LEGACY",
    ]),
    ("Papers", [
        "papers/01-reactive-engine", "papers/02-frontend-argus-hanka",
        "papers/03-live-graph", "papers/04-auth", "papers/05-middleware",
        "papers/06-oauth", "papers/07-execution", "papers/08-architecture",
        "papers/09-core-engines",
    ]),
]

# Documents that exist but are represented by the hub itself.
SKIP = {"README"}

TITLES: dict[str, str] = {
    "getting_started": "Getting started",
    "BUILD_MODE": "Build mode",
    "SYNTAX_V2_SPEC": "Syntax v2 specification",
    "SYNTAX_V2_PLAN": "Syntax v2 plan",
    "STRATEGY": "Strategy",
    "language_reference": "Language reference",
    "standard_library": "Standard library",
    "INTEGER": "Integer semantics",
    "AST": "AST",
    "advanced_topics": "Advanced topics",
    "REACTIVE": "Reactive",
    "REACTIVE_SPEC": "Reactive specification",
    "REACTIVE_ROADMAP": "Reactive roadmap",
    "LIVE_GRAPH": "Live Graph",
    "DEPLOY": "Deploy",
    "BACKEND_ROADMAP": "Backend roadmap",
    "AUTH": "Auth",
    "NETWORK_DB_ROADMAP": "Network & database",
    "BACKEND_BENCHMARKS": "Backend benchmarks",
    "BACKEND_PUBLISH": "Publishing a backend",
    "FRONTEND_ROADMAP": "Frontend roadmap",
    "ARGUS": "Argus",
    "HANKA": "Hanka",
    "LAYOUT_ENGINE": "Layout engine",
    "RENDERING_ENGINE": "Rendering engine",
    "PRODUCTION_WEB": "Production web",
    "EDITOR_TOOLING": "Editor tooling",
    "contributor_guide": "Contributor guide",
    "BENCHMARKS": "Benchmarks",
    "SCORECARD": "Scorecard",
    "LEGACY": "Legacy",
}


@dataclass
class Doc:
    slug: str          # url slug, e.g. "build-mode"
    key: str           # lookup key, e.g. "BUILD_MODE" or "papers/03-live-graph"
    source: str        # path relative to repo root
    title: str
    summary: str = ""
    body: str = ""
    toc: list[tuple[str, str]] = field(default_factory=list)


def slugify(key: str) -> str:
    name = key.split("/")[-1]
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def anchor(text: str) -> str:
    text = re.sub(r"<[^>]+>", "", text)
    text = html.unescape(text)
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


# ── Syntax highlighting ─────────────────────────────────────────────────

# Multi-word forms first so `push watch` never matches as bare `watch`.
LUKE_KEYWORDS = (
    r"push\s+watch|effect\s+on|gives\s+back|"
    "fn|let|var|return|if|else|while|for|struct|import|signal|derived|"
    "batch|watch|from|where|every|beats|raw|private|super|self|"
    "true|false|and|or|not|try|catch|throw|assert|arena|scope|on|in"
)
LUKE_TYPES = "int|float|str|bool|list|map|json|Server|Request|Db"

SHELL_HEADS = (
    r"git|cd|make|export|sudo|apt|dnf|brew|luke|node|python3|curl|bash|sh|"
    r"xcode-select|\./build/luke|\./hello|\./app"
)

# One pass, so a token can never be rewritten by a later rule. Order is
# precedence: comments and strings swallow everything inside them.
LUKE_TOKENS = re.compile(
    r"(?P<cm>//[^\n]*|\#[^\n]*)"
    r"|(?P<str>&quot;(?:[^&]|&(?!quot;))*&quot;)"
    rf"|(?P<ty>\b(?:{LUKE_TYPES})\b)"
    rf"|(?P<kw>\b(?:{LUKE_KEYWORDS})\b)"
    r"|(?P<num>\b\d+(?:\.\d+)?\b)"
    r"|(?P<fn>\b[A-Za-z_][A-Za-z0-9_]*(?=\())"
)


def highlight(code: str, lang: str) -> str:
    """Token markup matching the classes already defined in styles.css."""
    out = html.escape(code)

    if lang in {"luke", "lk"}:
        def paint(match: re.Match[str]) -> str:
            kind = match.lastgroup
            return f'<b class="{kind}">{match.group()}</b>'

        out = LUKE_TOKENS.sub(paint, out)

    elif lang in {"bash", "sh", "shell", "console"}:
        out = re.sub(r"(#[^\n]*)", r'<b class="cm">\1</b>', out)
        out = re.sub(rf"(?m)^(\s*)({SHELL_HEADS})\b", r'\1<b class="sh">\2</b>', out)

    lines = out.split("\n")
    while lines and not lines[-1].strip():
        lines.pop()
    return "".join(f'<span class="ln">{line}</span>' for line in lines)


FENCE_RE = re.compile(r"```([A-Za-z0-9_+-]*)\n(.*?)```", re.S)


def protect_code(text: str, blocks: list[str]) -> str:
    """Replace fenced blocks with placeholders so Markdown leaves them alone."""
    def take(match: re.Match[str]) -> str:
        lang = (match.group(1) or "").lower()
        blocks.append(f"<pre><code>{highlight(match.group(2), lang)}</code></pre>")
        return f"\n\nLUKEFENCE{len(blocks) - 1}ENDFENCE\n\n"

    return FENCE_RE.sub(take, text)


def restore_code(html_text: str, blocks: list[str]) -> str:
    def put(match: re.Match[str]) -> str:
        return blocks[int(match.group(1))]

    html_text = re.sub(r"<p>LUKEFENCE(\d+)ENDFENCE</p>", put, html_text)
    return re.sub(r"LUKEFENCE(\d+)ENDFENCE", put, html_text)


# ── Link rewriting ──────────────────────────────────────────────────────

def rewrite_links(body: str, doc: Doc, known: dict[str, str]) -> str:
    """Point Markdown links at hosted pages where one exists, GitHub otherwise."""
    def fix(match: re.Match[str]) -> str:
        href = match.group(1)
        if href.startswith(("http://", "https://", "#", "mailto:")):
            return match.group(0)

        target, _, frag = href.partition("#")
        frag = f"#{frag}" if frag else ""

        if not target:
            return match.group(0)

        base = os.path.basename(target)
        stem = base[:-3] if base.endswith(".md") else base

        # A paper links to a sibling paper.
        if doc.key.startswith("papers/") and target.endswith(".md") and "/" not in target:
            hosted = known.get(f"papers/{stem}")
            if hosted:
                return f'href="../{hosted}/{frag}"'

        hosted = known.get(stem)
        if hosted and target.endswith(".md"):
            return f'href="../{hosted}/{frag}"'

        # Anything else lives in the repository.
        doc_dir = os.path.dirname(doc.source)
        resolved = os.path.normpath(os.path.join(doc_dir, target))
        kind = "tree" if not os.path.splitext(resolved)[1] else "blob"
        return f'href="{REPO}/{kind}/main/{resolved}{frag}"'

    return re.sub(r'href="([^"]+)"', fix, body)


# ── Page shell ──────────────────────────────────────────────────────────

def nav(depth: int, current: str = "") -> str:
    up = "../" * depth
    items = [
        ("Learn", f"{up}learn/"),
        ("Docs", f"{up}docs/"),
        ("Download", f"{up}download/"),
        ("Examples", f"{up}examples/"),
        ("Community", f"{up}community/"),
        ("News", f"{up}news/"),
        ("GitHub", REPO),
    ]
    links = "\n".join(
        f'    <a href="{href}"{" aria-current=\"page\"" if label == current else ""}>{label}</a>'
        for label, href in items
    )
    return f"""<header class="nav" data-nav>
  <a class="nav__brand" href="{up}">
    <img src="{up}assets/luke-mark-sm.png" alt="" width="128" height="65" />
    <span>LukeLang</span>
  </a>
  <nav class="nav__links" aria-label="Primary">
{links}
  </nav>
</header>"""


def footer(depth: int) -> str:
    up = "../" * depth
    return f"""<footer class="footer">
  <div class="footer__mark">
    <img src="{up}assets/luke-wordmark-light.webp" alt="LukeLang" width="900" height="165" />
  </div>
  <div class="footer__grid">
    <p class="footer__line">
      Myanmar's first official programming language.<br />
      Designed and developed by <b>Kaung Myat San</b>.
    </p>
    <nav class="footer__nav" aria-label="Footer">
      <a href="{up}learn/">Learn</a>
      <a href="{up}docs/">Docs</a>
      <a href="{up}download/">Download</a>
      <a href="{up}examples/">Examples</a>
      <a href="{up}community/">Community</a>
      <a href="{up}news/">News</a>
      <a href="{STATUS}">Status</a>
      <a href="{REPO}">GitHub</a>
    </nav>
    <p class="footer__meta">lukelang.org · © <span data-year>2026</span></p>
  </div>
</footer>"""


def shell(*, depth: int, title: str, description: str, body: str) -> str:
    up = "../" * depth
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>{html.escape(title)} — LukeLang</title>
<meta name="description" content="{html.escape(description)}" />
<meta name="theme-color" content="#050806" />
<link rel="icon" href="{up}assets/favicon.png" type="image/png" />
<link rel="preconnect" href="https://fonts.googleapis.com" />
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin />
<link href="https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:ital,wght@0,200..800;1,200..800&display=swap" rel="stylesheet" />
<link rel="stylesheet" href="{up}styles.css" />
<link rel="stylesheet" href="{up}pages.css" />
<script>document.documentElement.className += ' js';</script>
</head>
<body>

<div class="grain" aria-hidden="true"></div>
<div class="scrollbar" aria-hidden="true"><i data-progress></i></div>

{nav(depth, "Docs")}

{body}

{footer(depth)}

<script src="{up}main.js"></script>
</body>
</html>
"""


def sidebar(docs: dict[str, Doc], current: str, depth: int) -> str:
    """Depth 2 for a doc page (site/docs/<slug>/), 1 for the hub."""
    up = "../" if depth == 2 else ""
    out = [f'      <h2>Documentation</h2>',
           f'      <ul><li><a href="{up}">All documents</a></li></ul>']
    for group, keys in GROUPS:
        present = [k for k in keys if k in docs]
        if not present:
            continue
        out.append(f"      <h2>{html.escape(group)}</h2>")
        out.append("      <ul>")
        for key in present:
            doc = docs[key]
            mark = ' aria-current="page"' if key == current else ""
            out.append(f'        <li><a href="{up}{doc.slug}/"{mark}>{html.escape(doc.title)}</a></li>')
        out.append("      </ul>")
    return "\n".join(out)


# ── Build ───────────────────────────────────────────────────────────────

def collect() -> dict[str, Doc]:
    docs: dict[str, Doc] = {}

    def add(key: str, source: str) -> None:
        name = key.split("/")[-1]
        if name in SKIP:
            return
        docs[key] = Doc(slug=slugify(key), key=key, source=source,
                        title=TITLES.get(key, ""))

    for name in sorted(os.listdir(os.path.join(ROOT, "docs"))):
        if name.endswith(".md"):
            add(name[:-3], f"docs/{name}")

    papers = os.path.join(ROOT, "documentations", "papers")
    if os.path.isdir(papers):
        for name in sorted(os.listdir(papers)):
            if name.endswith(".md"):
                add(f"papers/{name[:-3]}", f"documentations/papers/{name}")

    return docs


def resolve_titles(docs: dict[str, Doc]) -> None:
    """Titles must be known before any page renders, because every page
    prints the full sidebar."""
    for doc in docs.values():
        if doc.title:
            continue
        raw = open(os.path.join(ROOT, doc.source), encoding="utf-8").read()
        heading = re.search(r"(?m)^#\s+(.+)$", raw)
        doc.title = heading.group(1).strip() if heading else doc.key


def render(doc: Doc, docs: dict[str, Doc]) -> None:
    raw = open(os.path.join(ROOT, doc.source), encoding="utf-8").read()

    heading = re.search(r"(?m)^#\s+(.+)$", raw)
    if heading:
        raw = raw[:heading.start()] + raw[heading.end():]

    blocks: list[str] = []
    protected = protect_code(raw, blocks)

    md = markdown.Markdown(extensions=["tables", "sane_lists", "attr_list"])
    body = md.convert(protected)
    body = restore_code(body, blocks)

    # Anchor every h2 so the on-page contents can reach it.
    headings: list[tuple[str, str]] = []

    def anchor_h2(match: re.Match[str]) -> str:
        text = match.group(1)
        slug = anchor(text)
        headings.append((slug, re.sub(r"<[^>]+>", "", text)))
        return f'<h2 id="{slug}">{text}</h2>'

    body = re.sub(r"<h2>(.*?)</h2>", anchor_h2, body, flags=re.S)
    body = re.sub(r"<h3>(.*?)</h3>",
                  lambda m: f'<h3 id="{anchor(m.group(1))}">{m.group(1)}</h3>',
                  body, flags=re.S)

    body = rewrite_links(body, doc, {k: v.slug for k, v in docs.items()})

    first = re.search(r"<p>(.*?)</p>", body, re.S)
    doc.summary = re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", first.group(1))).strip() if first else ""

    contents = ""
    if len(headings) > 2:
        items = "\n".join(
            f'        <li><a href="#{slug}">{html.escape(text)}</a></li>'
            for slug, text in headings
        )
        contents = f"""
      <h2>On this page</h2>
      <ul>
{items}
      </ul>"""

    page = f"""<main>
  <section class="masthead masthead--doc">
    <p class="masthead__kicker"><a href="../">Documentation</a></p>
    <h1>{html.escape(doc.title)}</h1>
  </section>

  <div class="page">
    <aside class="sidebar">
{sidebar(docs, doc.key, 2)}{contents}
    </aside>

    <article class="prose prose--doc">
{body}
      <div class="next">
        <a href="../">← All documents</a>
        <a href="{REPO}/blob/main/{doc.source}">Edit this page on GitHub →</a>
      </div>
    </article>
  </div>
</main>"""

    summary = doc.summary or f"{doc.title} — LukeLang documentation."
    out_dir = os.path.join(OUT, doc.slug)
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "index.html"), "w", encoding="utf-8") as fh:
        fh.write(shell(depth=2, title=doc.title, description=summary[:180], body=page))


CHEATSHEET = [
    ("print(x)", "Write a value to standard output"),
    ("let n = 1", "Immutable binding; <b>var</b> for one you reassign"),
    ("let n: float = 1", "Typed binding — <b>int</b>, <b>float</b>, <b>str</b>, <b>bool</b>, "
                         "<b>list</b>, <b>map</b>, <b>json</b>"),
    ("fn f(a: float) -&gt; float { … }", "Function with a declared return type"),
    ("struct Dog : Animal { … }", "Blueprint with inheritance; <b>init</b> is the constructor"),
    ("import std/server", "Standard library, sibling file, or <b>luke/&lt;package&gt;</b>"),
    ("signal count = 0", "A reactive cell"),
    ("derived total = a * b", "A cell computed from other cells"),
    ("effect on total { … }", "Runs whenever the named cell changes"),
    ("batch { … }", "Several writes, one flush"),
    ("watch user from db where \"id = 1\"", "Server cell backed by a database row"),
    ("push watch user on req", "Stream that cell to a client over the wire"),
    ("bind(\"name\", user)", "Paint a cell into the DOM"),
    ("raw \"…\"", "Pass a line through to the v1 surface untouched"),
]


def cheatsheet_section() -> str:
    rows = "\n".join(
        f"            <tr><td>{form}</td><td>{meaning}</td></tr>"
        for form, meaning in CHEATSHEET
    )
    return f"""      <section id="syntax-at-a-glance">
        <h2>Syntax at a glance</h2>
        <p>
          Syntax v2 is the default for <code>.luke</code> and <code>.lk</code> files. The
          conversational v1 surface stays available behind <code>--syntax=1</code> during the
          deprecation window, and <code>luke MIGRATE</code> rewrites v1 source into v2.
        </p>
        <table class="sheet">
          <thead><tr><th>Form</th><th>Meaning</th></tr></thead>
          <tbody>
{rows}
          </tbody>
        </table>
      </section>"""


def render_hub(docs: dict[str, Doc]) -> None:
    sections = [cheatsheet_section()]
    nav_items = ['        <li><a href="#syntax-at-a-glance">Syntax at a glance</a></li>']
    for group, keys in GROUPS:
        present = [k for k in keys if k in docs]
        if not present:
            continue
        gid = anchor(group)
        nav_items.append(f'        <li><a href="#{gid}">{html.escape(group)}</a></li>')
        rows = "\n".join(
            f"""          <li>
            <p class="rows__key"><a href="{docs[k].slug}/">{html.escape(docs[k].title)}</a></p>
            <p class="rows__val">{html.escape(docs[k].summary[:170])}</p>
          </li>"""
            for k in present
        )
        sections.append(f"""      <section id="{gid}">
        <h2>{html.escape(group)}</h2>
        <ul class="rows">
{rows}
        </ul>
      </section>""")

    body = f"""<main>
  <section class="masthead">
    <p class="masthead__kicker">Documentation</p>
    <h1>Everything the compiler <em>already knows</em>.</h1>
    <p>
      {len(docs)} documents, hosted here and generated from the same Markdown that ships in the
      repository — so they move when the compiler moves.
    </p>
  </section>

  <div class="page">
    <aside class="sidebar">
      <h2>Sections</h2>
      <ul>
{chr(10).join(nav_items)}
      </ul>
      <h2>Elsewhere</h2>
      <ul>
        <li><a href="../learn/">Learn LukeLang</a></li>
        <li><a href="../examples/">Examples</a></li>
        <li><a href="../download/">Download</a></li>
      </ul>
    </aside>

    <div class="prose">
{chr(10).join(sections)}

      <div class="next">
        <a href="../learn/">← Learn the language</a>
        <a href="../examples/">Browse examples →</a>
      </div>
    </div>
  </div>
</main>"""

    with open(os.path.join(OUT, "index.html"), "w", encoding="utf-8") as fh:
        fh.write(shell(
            depth=1,
            title="Documentation",
            description="Every LukeLang document, hosted: the language, the reactive engine, "
                        "Live Graph, the backend and frontend surfaces, and the tooling.",
            body=body,
        ))


def main() -> int:
    docs = collect()
    resolve_titles(docs)
    os.makedirs(OUT, exist_ok=True)
    for doc in docs.values():
        render(doc, docs)
    render_hub(docs)
    print(f"build_site_docs: wrote {len(docs)} documents + hub to site/docs/")
    ungrouped = set(docs) - {k for _, keys in GROUPS for k in keys}
    if ungrouped:
        print("  not in any sidebar group:", ", ".join(sorted(ungrouped)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
