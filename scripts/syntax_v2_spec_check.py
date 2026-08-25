#!/usr/bin/env python3
"""Gate: every live conversational phrase must appear in docs/SYNTAX_V2_SPEC.md.

The spec is the mapping contract for the v2 front-end. If a phrase is live in the corpus but
absent from the spec, the front-end has no defined lowering for it and Phase 2 would silently
drop it. This is a solo project with no second reviewer, so the check is the review.

Exit 0 = every live phrase is covered (mapped or explicitly dropped).
Exit 1 = uncovered phrases, listed.

Usage:
    python3 scripts/syntax_v2_spec_check.py
    python3 scripts/syntax_v2_spec_check.py --list-covered
"""

import argparse
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = os.path.join(ROOT, "docs", "SYNTAX_V2_SPEC.md")
CODEGEN = os.path.join(ROOT, "vm", "src", "build_c.cpp")

CORPUS_GLOBS = [
    "examples/**/*.luke",
    "vm/stdlib/*.luke",
    "Exam/*.luke",
    "sample/*.luke",
    "luke_modules/**/*.luke",
    "registry/**/*.luke",
]

# Particles and prefixes that are not standalone statements. The spec covers them as argument
# particles / namespace prefixes rather than as their own mapping rows.
PARTICLES = {
    "AS", "OF", "ON", "AT", "UP", "WITH", "THE", "NOT", "ELSE", "DONE", "BORN",
    "STD/", "LUKE/", "PACKAGE:", "C:", "SELF.",
}


def read(path):
    with open(path, errors="ignore") as fh:
        return fh.read()


def live_phrases():
    codegen = set(re.findall(r'startsWithCI\([A-Za-z_]+, "([^"]+)"', read(CODEGEN)))
    files = []
    for pat in CORPUS_GLOBS:
        files += glob.glob(os.path.join(ROOT, pat), recursive=True)
    corpus = "\n".join(read(f).upper() for f in sorted(set(files)))
    out = set()
    for raw in codegen:
        p = raw.strip().upper()
        if not p or not re.match(r"^[A-Z]", p):
            continue
        if p in corpus:
            out.add(p)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list-covered", action="store_true")
    args = ap.parse_args()

    for path in (SPEC, CODEGEN):
        if not os.path.exists(path):
            sys.exit(f"missing required file: {path}")

    spec = read(SPEC).upper()
    phrases = live_phrases()

    covered, missing, particles = [], [], []
    for p in sorted(phrases):
        if p in PARTICLES:
            particles.append(p)
        elif p in spec:
            covered.append(p)
        else:
            missing.append(p)

    total = len(phrases)
    # Note: this counts distinct case-folded phrases, so pairs that differ only in casing
    # (PER IP / PER ip, c: / C:, std/ / STD/) collapse to one. syntax_surface_census.py counts
    # distinct string literals, so its total is slightly higher. Both are correct.
    print(f"live phrases           {total} (case-folded)")
    print(f"covered in spec        {len(covered)}")
    print(f"argument particles     {len(particles)} (covered generically)")
    print(f"uncovered              {len(missing)}")

    if args.list_covered:
        print("\ncovered:")
        for p in covered:
            print("   ", p)

    if missing:
        print("\nUNCOVERED — add a mapping row or an explicit drop in docs/SYNTAX_V2_SPEC.md:")
        for p in missing:
            print("   ", p)
        return 1

    print("\nspec_coverage_ok=1")
    return 0


if __name__ == "__main__":
    sys.exit(main())
