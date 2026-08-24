#!/usr/bin/env bash
# Prove debugger task 6: inspect reactive cell values + dependency edges.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LUKE="${LUKE:-$ROOT/vm/build/luke}"
if [[ ! -x "$LUKE" ]]; then
  echo "debug_inspect: missing luke at $LUKE (make -C vm first)" >&2
  exit 1
fi
if ! command -v gdb >/dev/null 2>&1; then
  echo "debug_inspect: gdb required" >&2
  exit 1
fi

SRC="$ROOT/examples/build/reactive_core.luke"
OUT="$ROOT/vm/build/ex_debug_inspect"

# Break on first print (line 5); DEBUG --inspect steps once so derived total has deps.
"$LUKE" DEBUG "$SRC" -o "$OUT" --break "$SRC:5" --inspect | tee /tmp/luke_debug_inspect.txt
grep -q "debug_inspect_ok=1" /tmp/luke_debug_inspect.txt
grep -q '"name":"price"' /tmp/luke_debug_inspect.txt
grep -q '"name":"quantity"' /tmp/luke_debug_inspect.txt
grep -q '"name":"total","kind":"derived"' /tmp/luke_debug_inspect.txt
grep -q '"name":"total","kind":"derived","value":"300","deps":\[{"id":1,"name":"price"},{"id":2,"name":"quantity"}\]' /tmp/luke_debug_inspect.txt

echo "debug_inspect_ok=1"
