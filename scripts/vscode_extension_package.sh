#!/usr/bin/env bash
# Package the official LukeLang VS Code extension (.vsix).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXT="$ROOT/tools/vscode/lukelang"
OUT="$EXT/dist"

cd "$EXT"
npm install --no-fund --no-audit
npm run check
npm run bundle
mkdir -p "$OUT"
npx vsce package -o "$OUT"
test -n "$(find "$OUT" -maxdepth 1 -name '*.vsix' -print -quit)"
echo "vscode_extension_package_ok=1"
