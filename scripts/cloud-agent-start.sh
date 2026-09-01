#!/usr/bin/env bash
# Per-boot services for LukeLang Cloud Agents.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export LUKE_WASI_SDK="${LUKE_WASI_SDK:-$ROOT/.tools/wasi-sdk}"
export PATH="$ROOT/vm/build:$PATH"

if command -v service >/dev/null 2>&1; then
  sudo service postgresql start >/dev/null 2>&1 || true
fi

if command -v pg_isready >/dev/null 2>&1; then
  for _ in $(seq 1 30); do
    pg_isready -h 127.0.0.1 -p 5432 >/dev/null 2>&1 && break
    sleep 0.2
  done
fi

sudo -u postgres psql -c "CREATE USER luke WITH PASSWORD 'luke' SUPERUSER;" >/dev/null 2>&1 || true
sudo -u postgres psql -c "CREATE DATABASE luke OWNER luke;" >/dev/null 2>&1 || true

echo "cloud-agent-start ok (postgres + LUKE_WASI_SDK)"
