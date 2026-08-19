#!/usr/bin/env bash
# Publish the official LukeLang VS Code extension to the Marketplace.
# Requires VSCE_PAT (Visual Studio Marketplace Personal Access Token).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [[ -z "${VSCE_PAT:-}" ]]; then
  echo "vscode_extension_publish: set VSCE_PAT (Marketplace PAT with Marketplace > Manage scope)" >&2
  exit 1
fi

bash "$ROOT/scripts/vscode_extension_package.sh"

EXT="$ROOT/tools/vscode/lukelang"
VSIX="$(find "$EXT/dist" -maxdepth 1 -name 'lukelang-vscode-*.vsix' | sort | tail -1)"
if [[ -z "$VSIX" ]]; then
  echo "vscode_extension_publish: no .vsix found in $EXT/dist" >&2
  exit 1
fi

cd "$EXT"
npx vsce publish --packagePath "$VSIX"
echo "vscode_extension_publish_ok=1"
