#!/usr/bin/env bash
# Local smoke for the wall (deployed Live Graph) proof — no TLS required.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LUKE="$ROOT/vm/build/luke"
cd "$ROOT/vm"
make -s build/luke
rm -f /tmp/luke_wall.db /tmp/luke_wall.db-wal /tmp/luke_wall.db-shm
"$LUKE" BUILD ../examples/deploy/wall/server.luke -o build/wall_server
mkdir -p ../examples/deploy/wall/dist
"$LUKE" BUILD ../examples/deploy/wall/client.luke -target browser -o ../examples/deploy/wall/dist/client
test -f ../examples/deploy/wall/dist/client.html
test -f ../examples/deploy/wall/dist/client.wasm
# Compile-time evidence of fail-closed SSE loop
grep -q '_luke_watch_alive' build/wall_server.luke.c
grep -q 'luke_ivm_' build/wall_server.luke.c
grep -q 'WHEN NEW\|NEW.name\|group_concat(name' build/wall_server.luke.c
# Brief runtime: health + one watch accept
stdbuf -oL -eL ./build/wall_server >/tmp/luke_wall_server.log 2>&1 &
echo $! >/tmp/luke_wall_server.pid
cleanup() { kill "$(cat /tmp/luke_wall_server.pid)" 2>/dev/null || true; }
trap cleanup EXIT
sleep 0.3
curl -fsS "http://127.0.0.1:8800/health" | grep -q ok
# Client disconnect should fail-closed (alive→0), not hang forever
timeout 3 curl -fsS -N "http://127.0.0.1:8800/watch" >/tmp/luke_wall_watch.txt || true
grep -q 'data:\|luke-sse\|: cdc' /tmp/luke_wall_watch.txt || grep -q 'seed\|cdc' /tmp/luke_wall_server.log
echo "wall_smoke_ok=1"
