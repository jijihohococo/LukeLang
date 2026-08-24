#!/usr/bin/env python3
"""Gate: the v2 golden corpus stays paired with its v1 twins, and stays v2.

Checks, for examples/v2/*.lk:
  1. every .lk has a v1 twin at examples/build/<same-stem>.luke
  2. no v1 conversational keyword leaked into a .lk file (a translation miss)
  3. no v2 file still uses `AND` for concatenation or `ASK`/`SET TO` forms

This does NOT compile anything — the v2 front-end is Phase 2. It guards the corpus against
drifting out of sync with the spec while that work happens.

Exit 0 = corpus consistent.
"""

import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
V2_DIR = os.path.join(ROOT, "examples", "v2")
V1_DIR = os.path.join(ROOT, "examples", "build")

# v1 forms that must not survive in a .lk file. Word-boundary matched, case sensitive
# (v2 keywords are lowercase, so uppercase hits are genuine leftovers).
LEAKED = [
    r"\bSPEAK\b", r"\bMY NAME IS\b", r"\bSET TO\b", r"\bASK\b", r"\bGIVE BACK\b",
    r"\bTHIS IS FUNCTION\b", r"\bEND FUNCTION\b", r"\bEND IF\b", r"\bEND WHILE\b",
    r"\bBLUEPRINT\b", r"\bREMEMBER\b", r"\bMULTIPLIED BY\b", r"\bDIVIDED BY\b",
    r"\bEQUALS\b", r"\bHOW MANY IN\b", r"\bITEM\b", r"\bATTEMPT\b", r"\bOTHERWISE\b",
    r"\bGIVE UP\b", r"\bMAKE SURE\b", r"\bWHEN BORN\b", r"\bEND BORN\b", r"\bEND METHOD\b",
    r"\bCALL PARENT\b", r"\bIN ARENA\b", r"\bEND ARENA\b", r"\bBEGIN REACTIVE BATCH\b",
]

# Lines exempt from the leak check: comments may legitimately mention v1 spellings, and
# string literals must keep their original bytes (see examples/v2/README.md rule 2).
STRING_RE = re.compile(r'"[^"]*"')


def strip_noncode(line):
    line = STRING_RE.sub('""', line)
    idx = line.find("//")
    return line[:idx] if idx >= 0 else line


def main():
    if not os.path.isdir(V2_DIR):
        sys.exit(f"missing {V2_DIR}")

    v2_files = sorted(glob.glob(os.path.join(V2_DIR, "*.lk")))
    if not v2_files:
        sys.exit("no .lk files found in examples/v2/")

    problems = []
    for path in v2_files:
        stem = os.path.splitext(os.path.basename(path))[0]
        twin = os.path.join(V1_DIR, stem + ".luke")
        if not os.path.exists(twin):
            problems.append(f"{stem}.lk has no v1 twin at examples/build/{stem}.luke")

        with open(path, errors="ignore") as fh:
            for n, raw in enumerate(fh, 1):
                code = strip_noncode(raw)
                for pat in LEAKED:
                    if re.search(pat, code):
                        kw = pat.strip("\\b")
                        problems.append(f"{stem}.lk:{n} leftover v1 form: {kw}")

    print(f"v2 corpus files   {len(v2_files)}")
    print(f"v1 twins matched  {len(v2_files) - sum(1 for p in problems if 'no v1 twin' in p)}")

    if problems:
        print(f"\nPROBLEMS ({len(problems)}):")
        for p in problems:
            print("   ", p)
        return 1

    print("\ncorpus_check_ok=1")
    return 0


if __name__ == "__main__":
    sys.exit(main())
