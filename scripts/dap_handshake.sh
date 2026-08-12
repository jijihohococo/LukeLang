#!/usr/bin/env bash
# Prove debugger task 7: DAP stdio handshake (initialize → initialized event).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LUKE="${LUKE:-$ROOT/vm/build/luke}"
if [[ ! -x "$LUKE" ]]; then
  echo "dap_handshake: missing luke at $LUKE (make -C vm first)" >&2
  exit 1
fi

REQ='{"seq":1,"type":"request","command":"initialize","arguments":{"adapterID":"luke","linesStartAt1":true,"columnsStartAt1":true,"pathFormat":"path"}}'
BODY_LEN=${#REQ}
{
  printf 'Content-Length: %s\r\n\r\n%s' "$BODY_LEN" "$REQ"
  # disconnect so the adapter exits cleanly
  DISC='{"seq":2,"type":"request","command":"disconnect","arguments":{}}'
  printf 'Content-Length: %s\r\n\r\n%s' "${#DISC}" "$DISC"
} | "$LUKE" DAP > /tmp/luke_dap_handshake.txt 2>/tmp/luke_dap_handshake.err || true

grep -q '"event":"initialized"' /tmp/luke_dap_handshake.txt
grep -q '"command":"initialize"' /tmp/luke_dap_handshake.txt
grep -q '"success":true' /tmp/luke_dap_handshake.txt

echo "dap_handshake_ok=1"
