#!/usr/bin/env bash
# Idempotent Cloud Agent bootstrap for LukeLang.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "→ python deps"
python3 -m pip install --user --quiet markdown paramiko

echo "→ WASI SDK"
VER=22.0
WASI_DIR="$ROOT/.tools/wasi-sdk"
if [ ! -x "$WASI_DIR/bin/clang" ]; then
  mkdir -p "$ROOT/.tools"
  curl -fsSL "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-22/wasi-sdk-${VER}-linux.tar.gz" \
    -o /tmp/wasi-sdk.tgz
  tar -xzf /tmp/wasi-sdk.tgz -C /tmp
  rm -rf "$WASI_DIR"
  mv "/tmp/wasi-sdk-${VER}" "$WASI_DIR"
fi

echo "→ build luke"
export LUKE_WASI_SDK="$WASI_DIR"
make -C vm -j"$(nproc 2>/dev/null || echo 2)"

echo "→ smoke: hello BUILD"
"$ROOT/vm/build/luke" BUILD "$ROOT/examples/build/hello.luke" -o /tmp/luke-hello-check
"$ROOT/vm/build/luke" SHOW "$ROOT/examples/build/hello.luke" | grep -qi hello

echo "→ site generators"
python3 scripts/build_site_docs.py
python3 scripts/build_site_meta.py

echo "cloud-agent-install ok"
