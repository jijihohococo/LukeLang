#!/usr/bin/env python3
"""Measure the conversational syntax surface — inputs for docs/SYNTAX_V2_PLAN.md.

Reports:
  1. distinct conversational phrases hard-coded in codegen / statement parser
  2. which of those phrases are live in the corpus vs dead
  3. statement-kind distribution from `luke IR`, app-level vs stdlib
  4. the StmtKind::Raw tail, ranked — the Phase 3a work queue

Usage:
    cd vm && make
    python3 scripts/syntax_surface_census.py
    python3 scripts/syntax_surface_census.py --raw-queue   # Phase 3a priority list
"""

import argparse
import collections
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LUKE = os.environ.get("LUKE", os.path.join(ROOT, "vm", "build", "luke"))

PHRASE_RE = re.compile(r'startsWithCI\([A-Za-z_]+, "([^"]+)"')
PARSE_PHRASE_RE = re.compile(r'startsCI\([A-Za-z_]+, "([^"]+)"')


def read(path):
    with open(path, errors="ignore") as fh:
        return fh.read()


def phrase_surface():
    codegen = sorted(set(PHRASE_RE.findall(read(os.path.join(ROOT, "vm/src/build_c.cpp")))))
    parser = sorted(set(PARSE_PHRASE_RE.findall(read(os.path.join(ROOT, "vm/src/luke_parse.cpp")))))
    return codegen, parser


def corpus_files():
    pats = [
        "examples/**/*.luke",
        "vm/stdlib/*.luke",
        "Exam/*.luke",
        "sample/*.luke",
        "luke_modules/**/*.luke",
        "registry/**/*.luke",
    ]
    out = []
    for p in pats:
        out += glob.glob(os.path.join(ROOT, p), recursive=True)
    return sorted(set(out))


def live_dead(phrases, corpus_text):
    live, dead = [], []
    for raw in phrases:
        p = raw.strip().upper()
        if not p or not re.match(r"^[A-Z]", p):
            continue
        (live if p in corpus_text else dead).append(raw.strip())
    return live, dead


# stdlib resolves relative to cwd ("stdlib/" or "vm/stdlib/"), so match either.
STDLIB_RE = re.compile(r"file=\S*stdlib/")


def ir_ast(path):
    """Return the `--- ast ---` section of `luke IR <path>`, or None."""
    try:
        out = subprocess.run(
            [LUKE, "IR", path],
            capture_output=True,
            text=True,
            timeout=60,
            cwd=os.path.join(ROOT, "vm"),
        ).stdout
    except Exception:
        return None
    if "--- ast ---" not in out:
        return None
    return out.split("--- ast ---", 1)[1]


def stmt_census(files):
    all_kinds = collections.Counter()
    app_kinds = collections.Counter()
    raw_sites = []
    failures = 0
    for f in files:
        ast = ir_ast(f)
        if ast is None:
            failures += 1
            continue
        for line in ast.splitlines():
            s = line.strip()
            if not s or s.startswith("luke-ast-program"):
                continue
            m = re.match(r"([A-Za-z]+)", s)
            if not m:
                continue
            kind = m.group(1)
            all_kinds[kind] += 1
            if not STDLIB_RE.search(s):
                app_kinds[kind] += 1
                if kind == "Raw":
                    loc = re.search(r"line=(\d+) file=(\S+)", s)
                    if loc:
                        raw_sites.append((loc.group(2), int(loc.group(1))))
    return all_kinds, app_kinds, raw_sites, failures


def source_line(path, n, cache={}):
    # IR reports paths relative to vm/ (that is the cwd we invoke luke from).
    if path not in cache:
        resolved = path if os.path.isabs(path) else os.path.join(ROOT, "vm", path)
        try:
            cache[path] = read(resolved).splitlines()
        except Exception:
            cache[path] = []
    lines = cache[path]
    return lines[n - 1].strip() if 0 < n <= len(lines) else ""


def classify_raw(raw_sites):
    forms = collections.Counter()
    examples = {}
    for path, n in raw_sites:
        txt = source_line(path, n)
        if not txt or txt.startswith("//"):
            continue
        words = re.findall(r"[A-Za-z\"']+", txt.upper())
        key = " ".join(words[:2]) if words else "???"
        forms[key] += 1
        examples.setdefault(key, txt[:70])
    return forms, examples


def pct(n, d):
    return f"{100.0 * n / d:.1f}%" if d else "n/a"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--raw-queue",
        action="store_true",
        help="print the ranked StmtKind::Raw work queue for Phase 3a",
    )
    args = ap.parse_args()

    if not os.path.exists(LUKE):
        sys.exit(f"luke binary not found at {LUKE} — run: cd vm && make")

    codegen, parser = phrase_surface()
    files = corpus_files()
    corpus_text = "\n".join(read(f).upper() for f in files)
    live, dead = live_dead(codegen, corpus_text)

    examples = sorted(glob.glob(os.path.join(ROOT, "examples/build/*.luke")))
    all_kinds, app_kinds, raw_sites, failures = stmt_census(examples)
    forms, form_examples = classify_raw(raw_sites)

    if args.raw_queue:
        total = sum(forms.values())
        print(f"Phase 3a work queue — {len(forms)} distinct forms, {total} statements\n")
        print(f"{'count':>6}  {'cum':>6}  form / example")
        cum = 0
        for key, n in forms.most_common():
            cum += n
            print(f"{n:>6}  {pct(cum, total):>6}  {key:<22} {form_examples.get(key,'')}")
        return

    print("=" * 66)
    print("LukeLang conversational syntax surface census")
    print("=" * 66)

    print(f"\ncorpus: {len(files)} .luke files, {sum(len(read(f).splitlines()) for f in files)} lines")

    print("\n-- phrase surface -------------------------------------------------")
    print(f"  codegen (build_c.cpp)      {len(codegen):>4} distinct phrases")
    print(f"  statement parser           {len(parser):>4} distinct phrases")
    print(f"  live in corpus             {len(live):>4}")
    print(f"  dead (drop, do not port)   {len(dead):>4}  ({pct(len(dead), len(live)+len(dead))})")

    print("\n-- statement kinds (examples/build) -------------------------------")
    tot_all = sum(all_kinds.values())
    tot_app = sum(app_kinds.values())
    print(f"  all nodes (incl. stdlib)   {tot_all:>5}   Raw {all_kinds['Raw']:>4} ({pct(all_kinds['Raw'], tot_all)})")
    print(f"  app-level only             {tot_app:>5}   Raw {app_kinds['Raw']:>4} ({pct(app_kinds['Raw'], tot_app)})")
    if failures:
        print(f"  IR failures                {failures}")

    print("\n  app-level distribution:")
    for kind, n in app_kinds.most_common(12):
        print(f"    {kind:<14} {n:>5}  {pct(n, tot_app):>6}")

    print("\n-- Raw tail (Phase 3a cost centre) --------------------------------")
    tot_forms = sum(forms.values())
    print(f"  distinct leading forms     {len(forms):>4}")
    print(f"  statements                 {tot_forms:>4}")
    if forms:
        top, n = forms.most_common(1)[0]
        print(f"  most common form           {top!r} at {pct(n, tot_forms)} — no 80/20 shortcut")
    print("\n  top 10:")
    for key, n in forms.most_common(10):
        print(f"    {n:>4}  {key:<22} {form_examples.get(key,'')}")

    print("\nrun with --raw-queue for the full ranked Phase 3a list")


if __name__ == "__main__":
    main()
