#!/usr/bin/env bash
# Build DevOps dashboard (Luke API + TypeScript UI → site/ops/)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"

echo "→ building LukeLang API"
cd vm
make build/luke
./build/luke BUILD ../apps/devops/backend/main.lk -o ../apps/devops/backend/devops_api
cd "$ROOT"

echo "→ building TypeScript frontend"
cd apps/devops/frontend
npm install
npm run build
cd "$ROOT"

echo "done — static UI: site/ops/  API binary: apps/devops/backend/devops_api"
