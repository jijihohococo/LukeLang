#!/usr/bin/env bash
# Phase 2 acceptance gate: examples/v2/*.lk must be equivalent to their v1 twins.
#
# The gate defined by docs/SYNTAX_V2_SPEC.md §11.3 is byte-identical stdout. Server
# programs block on accept() and cannot be run unattended, so for those the gate is
# instead the generated C compared modulo #line directives — weaker than running, but
# strong evidence that the same program was produced.
#
# The C comparison is also reported for runnable programs as extra confirmation. A C
# difference alone is NOT a failure: v1 concatenation is right-associative while the v2
# lowerer emits explicit left-grouped parentheses, so `a + b + c` yields a different tree
# with identical semantics. Arithmetic is left-associative in both and the lowerer always
# parenthesises, so non-associative operators (`-`, `/`) cannot be misgrouped.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LUKE="${LUKE:-$ROOT/vm/build/luke}"
WORK="${WORK:-/tmp/luke_v2_equiv}"

if [[ ! -x "$LUKE" ]]; then
  echo "syntax_v2_equiv: missing luke at $LUKE (make -C vm first)" >&2
  exit 1
fi

SERVERS=" backend_api auth_api live_graph_server dashboard_server concurrent_server "
PROVISIONAL=""   # layout DSL (§6) still provisional; golden uses raw passthrough

mkdir -p "$WORK"
pass=0; fail=0; prov=0
failed=(); provskip=()

note() { printf '%-20s %-5s %s\n' "$1" "$2" "$3"; }

for v2 in "$ROOT"/examples/v2/*.lk; do
  stem="$(basename "$v2" .lk)"
  v1="$ROOT/examples/v1_archive/build/$stem.luke"
  is_prov=0
  [[ "$PROVISIONAL" == *" $stem "* ]] && is_prov=1
  is_server=0
  [[ "$SERVERS" == *" $stem "* ]] && is_server=1

  if [[ ! -f "$v1" ]]; then
    note "$stem" FAIL "no v1 twin at examples/v1_archive/build/$stem.luke"
    fail=$((fail+1)); failed+=("$stem"); continue
  fi

  b1="$WORK/${stem}_v1"; b2="$WORK/${stem}_v2"
  e1="$WORK/${stem}_v1.err"; e2="$WORK/${stem}_v2.err"

  if ! (cd "$ROOT/vm" && "$LUKE" --syntax=1 BUILD "$v1" -o "$b1" >/dev/null 2>"$e1"); then
    note "$stem" FAIL "v1 build: $(head -1 "$e1")"
    fail=$((fail+1)); failed+=("$stem"); continue
  fi
  if ! (cd "$ROOT/vm" && "$LUKE" BUILD "$v2" -o "$b2" >/dev/null 2>"$e2"); then
    if (( is_prov )); then
      note "$stem" prov "not lowered (spec §6): $(head -1 "$e2")"
      prov=$((prov+1)); provskip+=("$stem"); continue
    fi
    note "$stem" FAIL "v2 build: $(head -1 "$e2")"
    fail=$((fail+1)); failed+=("$stem"); continue
  fi

  # secondary signal: generated C modulo #line
  csame="C:differs"
  if grep -v '^#line' "$b1.luke.c" > "$WORK/$stem.c1" 2>/dev/null &&
     grep -v '^#line' "$b2.luke.c" > "$WORK/$stem.c2" 2>/dev/null &&
     diff -q "$WORK/$stem.c1" "$WORK/$stem.c2" >/dev/null 2>&1; then
    csame="C:identical"
  fi

  if (( is_server )); then
    if [[ "$csame" == "C:identical" ]]; then
      note "$stem" ok "[C identical, $(wc -l < "$WORK/$stem.c1") lines] server — not run"
      pass=$((pass+1))
    else
      note "$stem" FAIL "server: generated C differs and it cannot be run"
      diff "$WORK/$stem.c1" "$WORK/$stem.c2" | head -10 | sed 's/^/        /'
      fail=$((fail+1)); failed+=("$stem")
    fi
    continue
  fi

  o1="$(cd "$ROOT/vm" && timeout 20 "$b1" 2>&1)"; r1=$?
  o2="$(cd "$ROOT/vm" && timeout 20 "$b2" 2>&1)"; r2=$?
  if [[ "$o1" == "$o2" && "$r1" == "$r2" ]]; then
    note "$stem" ok "[stdout ${#o1}B identical, exit $r1] $csame"
    pass=$((pass+1))
  else
    if (( is_prov )); then
      note "$stem" prov "stdout differs (spec §6)"
      prov=$((prov+1)); provskip+=("$stem"); continue
    fi
    note "$stem" FAIL "stdout differs (exit $r1 vs $r2)"
    diff <(printf '%s\n' "$o1") <(printf '%s\n' "$o2") | head -10 | sed 's/^/        /'
    fail=$((fail+1)); failed+=("$stem")
  fi
done

echo
echo "normative: pass=$pass fail=$fail    provisional (spec §6): $prov"
(( prov > 0 )) && echo "provisional, not yet lowered: ${provskip[*]}"
if (( fail > 0 )); then
  echo "FAILING: ${failed[*]}"
  exit 1
fi
echo "syntax_v2_equiv_ok=1"
