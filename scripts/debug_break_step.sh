#!/usr/bin/env bash
# Prove debugger tasks 4–5: breakpoints by .luke:line + step over/into/out.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LUKE="${LUKE:-$ROOT/vm/build/luke}"
if [[ ! -x "$LUKE" ]]; then
  echo "debug_break_step: missing luke at $LUKE (make -C vm first)" >&2
  exit 1
fi
if ! command -v gdb >/dev/null 2>&1; then
  echo "debug_break_step: gdb required" >&2
  exit 1
fi

SRC="$ROOT/examples/build/functions.luke"
OUT="$ROOT/vm/build/ex_debug_functions"

# Break on SPEAK "2 + 3 =" (line 10): next→11, next→13, step→greet:6, finish→13
"$LUKE" DEBUG "$SRC" -o "$OUT" --break "$SRC:10" --batch | tee /tmp/luke_debug_break_step.txt
grep -q "debug_break_step_ok=1" /tmp/luke_debug_break_step.txt
grep -q "LUKE_DBG_BREAK hit" /tmp/luke_debug_break_step.txt
grep -q "LUKE_DBG_STEP ok" /tmp/luke_debug_break_step.txt
grep -q "LUKE_DBG_FINISH ok" /tmp/luke_debug_break_step.txt
# Step-into must enter greet (Luke function), not stay in main
grep -qE 'greet \(.*\) at .*:6' /tmp/luke_debug_break_step.txt

echo "debug_break_step_ok=1"
