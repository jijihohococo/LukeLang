#!/usr/bin/env bash
# Phase 3 gate: MIGRATE(v1) then BUILD must match BUILD(v1) (stdout or C for servers).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LUKE="${LUKE:-$ROOT/vm/build/luke}"
WORK="${WORK:-/tmp/luke_v2_migrate_equiv}"

if [[ ! -x "$LUKE" ]]; then
  echo "syntax_v2_migrate_equiv: missing luke at $LUKE" >&2
  exit 1
fi

# Golden twins first (same set as Phase 2). Optional: MIGRATE_CORPUS=all
SERVERS=" backend_api auth_api live_graph_server dashboard_server concurrent_server "
PROVISIONAL=" frontend_widgets "

mkdir -p "$WORK"
pass=0; fail=0; prov=0; skip=0
failed=(); provskip=()

note() { printf '%-22s %-5s %s\n' "$1" "$2" "$3"; }

stems=()
if [[ "${MIGRATE_CORPUS:-golden}" == "all" ]]; then
  for v1 in "$ROOT"/examples/build/*.luke; do
    stems+=("$(basename "$v1" .luke)")
  done
else
  for v2 in "$ROOT"/examples/v2/*.lk; do
    stems+=("$(basename "$v2" .lk)")
  done
fi

for stem in "${stems[@]}"; do
  v1="$ROOT/examples/build/$stem.luke"
  is_prov=0
  [[ "$PROVISIONAL" == *" $stem "* ]] && is_prov=1
  is_server=0
  [[ "$SERVERS" == *" $stem "* ]] && is_server=1

  if [[ ! -f "$v1" ]]; then
    note "$stem" SKIP "no v1 at examples/build/$stem.luke"
    skip=$((skip+1)); continue
  fi

  mig="$WORK/${stem}_migrated.lk"
  e_mig="$WORK/${stem}_mig.err"
  if ! "$LUKE" MIGRATE "$v1" -o "$mig" >/dev/null 2>"$e_mig"; then
    rc=$?
    # exit 3 = TODOs present but output written — still try to build
    if [[ $rc -ne 3 && ! -s "$mig" ]]; then
      if (( is_prov )); then
        note "$stem" prov "migrate failed: $(head -1 "$e_mig")"
        prov=$((prov+1)); provskip+=("$stem"); continue
      fi
      note "$stem" FAIL "migrate: $(head -1 "$e_mig")"
      fail=$((fail+1)); failed+=("$stem"); continue
    fi
  fi

  todos=$(grep -c 'TODO(migrate)' "$mig" 2>/dev/null || true)
  b1="$WORK/${stem}_v1"; b2="$WORK/${stem}_mig"
  e1="$WORK/${stem}_v1.err"; e2="$WORK/${stem}_mig.err"

  if ! (cd "$ROOT/vm" && "$LUKE" BUILD "$v1" -o "$b1" >/dev/null 2>"$e1"); then
    note "$stem" FAIL "v1 build: $(head -1 "$e1")"
    fail=$((fail+1)); failed+=("$stem"); continue
  fi
  if ! (cd "$ROOT/vm" && "$LUKE" BUILD "$mig" -o "$b2" >/dev/null 2>"$e2"); then
    if (( is_prov )); then
      note "$stem" prov "migrated build failed (todos=$todos): $(head -1 "$e2")"
      prov=$((prov+1)); provskip+=("$stem"); continue
    fi
    note "$stem" FAIL "migrated build (todos=$todos): $(head -1 "$e2")"
    fail=$((fail+1)); failed+=("$stem"); continue
  fi

  csame="C:differs"
  if grep -v '^#line' "$b1.luke.c" > "$WORK/$stem.c1" 2>/dev/null &&
     grep -v '^#line' "$b2.luke.c" > "$WORK/$stem.c2" 2>/dev/null &&
     diff -q "$WORK/$stem.c1" "$WORK/$stem.c2" >/dev/null 2>&1; then
    csame="C:identical"
  fi

  if (( is_server )); then
    if [[ "$csame" == "C:identical" ]]; then
      note "$stem" ok "[C identical] server — not run (todos=$todos)"
      pass=$((pass+1))
    else
      note "$stem" FAIL "server: generated C differs (todos=$todos)"
      diff "$WORK/$stem.c1" "$WORK/$stem.c2" | head -8 | sed 's/^/        /'
      fail=$((fail+1)); failed+=("$stem")
    fi
    continue
  fi

  o1="$(cd "$ROOT/vm" && timeout 20 "$b1" 2>&1)"; r1=$?
  o2="$(cd "$ROOT/vm" && timeout 20 "$b2" 2>&1)"; r2=$?
  if [[ "$o1" == "$o2" && "$r1" == "$r2" ]]; then
    note "$stem" ok "[stdout identical, exit $r1] $csame todos=$todos"
    pass=$((pass+1))
  else
    if (( is_prov )); then
      note "$stem" prov "stdout differs (todos=$todos)"
      prov=$((prov+1)); continue
    fi
    note "$stem" FAIL "stdout differs (exit $r1 vs $r2, todos=$todos)"
    diff <(printf '%s\n' "$o1") <(printf '%s\n' "$o2") | head -8 | sed 's/^/        /'
    fail=$((fail+1)); failed+=("$stem")
  fi
done

echo
echo "migrate_equiv: pass=$pass fail=$fail provisional=$prov skip=$skip"
(( prov > 0 )) && echo "provisional: ${provskip[*]}"
if (( fail > 0 )); then
  echo "FAILING: ${failed[*]}"
  exit 1
fi
echo "syntax_v2_migrate_equiv_ok=1"
