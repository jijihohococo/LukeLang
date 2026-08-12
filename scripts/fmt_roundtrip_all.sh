#!/usr/bin/env bash
# FMT round-trip CI: for every examples/build/*.luke,
#   1) luke FMT must succeed
#   2) if the original native-builds, the formatted file must native-build
#   3) if the original binary exits quickly with stdout, the formatted binary
#      must produce identical stdout (after redacting per-run entropy)
# Known compile-fail examples must still fail to build after FMT.
# Long-running servers / interactive listeners are build-checked only (no run).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LUKE="${LUKE:-$ROOT/vm/build/luke}"
EX_DIR="$ROOT/examples/build"
WORK="${TMPDIR:-/tmp}/luke_fmt_rt_$$"
BUILD_TIMEOUT=45
RUN_TIMEOUT=8

if [[ ! -x "$LUKE" ]]; then
  echo "fmt_roundtrip_all: missing luke binary at $LUKE (run make -C vm first)" >&2
  exit 1
fi

mkdir -p "$WORK/orig" "$WORK/fmt" "$WORK/bin"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

# Copy the whole examples/build tree so relative IMPORTs resolve.
cp -a "$EX_DIR/." "$WORK/orig/"
cp -a "$EX_DIR/." "$WORK/fmt/"

is_negative() {
  local b="$1"
  case "$b" in
    bad_*|*_bad_*|*_bad.luke) return 0 ;;
  esac
  return 1
}

# Binaries that listen / loop — BUILD after FMT, but do not compare run output.
is_serverish() {
  local b="$1"
  case "$b" in
    *server*|concurrent_*|*pressure*|http_c10k*|dashboard_*|subscribe_*| \
    fullstack_cell_server*|backend_routes_serve*|live_graph_server*| \
    live_graph_updater*|wall_*) return 0 ;;
  esac
  if grep -qE 'ASK httpListen|ASK httpServe|httpAccept WITH' "$WORK/orig/$b" 2>/dev/null; then
    case "$b" in
      http_demo.luke|http_json.luke|http_server_demo.luke) return 0 ;;
    esac
  fi
  return 1
}

is_browserish() {
  local b="$1"
  case "$b" in
    *_client.luke|*_browser.luke|hello_js.luke|hello_wasm.luke|hello_browser.luke| \
    *_scrub.luke|reactive_greeting.luke|reactive_counter.luke|reactive_fetch.luke| \
    reactive_list_ui.luke|reactive_timeline_ui.luke|argus_demo.luke|hanka_demo.luke| \
    web_app.luke|frontend_widgets.luke|frontend_wrap_forms.luke|syntax_stress.luke| \
    hanka_align.luke) return 0 ;;
  esac
  return 1
}

normalize_out() {
  # Redact per-run entropy so FMT round-trip compares semantics, not RNG.
  sed -E \
    -e 's/[0-9a-f]{32}/<id>/g' \
    -e 's/\$argon2id\$[^[:space:]]+/\$argon2id\$<hash>/g' \
    -e 's/ saw [0-9]+/ saw <ts>/g' \
    -e 's/ms=[0-9.]+/ms=<t>/g' \
    -e 's/reveal [0-9]+/reveal <ts>/g' \
    "$1"
}

fmt_count=0
build_count=0
run_count=0
neg_count=0
browser_count=0
skip_run=0

echo "fmt_roundtrip_all: formatting every example…"
shopt -s nullglob
for f in "$WORK/fmt"/*.luke; do
  base="$(basename "$f")"
  if ! "$LUKE" FMT "$WORK/orig/$base" >"$f" 2>"$WORK/fmt_err_$base"; then
    echo "FAIL FMT $base" >&2
    cat "$WORK/fmt_err_$base" >&2 || true
    exit 1
  fi
  fmt_count=$((fmt_count + 1))
done
echo "  formatted $fmt_count files"

echo "fmt_roundtrip_all: build + output checks…"
for f in "$WORK/orig"/*.luke; do
  base="$(basename "$f")"
  orig="$WORK/orig/$base"
  fmtf="$WORK/fmt/$base"
  obin="$WORK/bin/o_${base%.luke}"
  fbin="$WORK/bin/f_${base%.luke}"

  if is_negative "$base"; then
    if "$LUKE" BUILD "$orig" -o "$obin" >"$WORK/neg_o_$base.txt" 2>&1; then
      echo "FAIL $base: expected original compile failure, but BUILD succeeded" >&2
      exit 1
    fi
    if "$LUKE" BUILD "$fmtf" -o "$fbin" >"$WORK/neg_f_$base.txt" 2>&1; then
      echo "FAIL $base: FMT'd file compiled but original was a negative example" >&2
      exit 1
    fi
    neg_count=$((neg_count + 1))
    continue
  fi

  if is_browserish "$base"; then
    if timeout "$BUILD_TIMEOUT" "$LUKE" BUILD "$orig" -target browser -o "$obin" \
        >"$WORK/br_o_$base.txt" 2>&1; then
      if ! timeout "$BUILD_TIMEOUT" "$LUKE" BUILD "$fmtf" -target browser -o "$fbin" \
          >"$WORK/br_f_$base.txt" 2>&1; then
        echo "FAIL $base: original browser BUILD ok, FMT'd browser BUILD failed" >&2
        tail -20 "$WORK/br_f_$base.txt" >&2 || true
        exit 1
      fi
      browser_count=$((browser_count + 1))
    fi
    if timeout "$BUILD_TIMEOUT" "$LUKE" BUILD "$orig" -o "$obin" \
        >"$WORK/nat_o_$base.txt" 2>&1; then
      if ! timeout "$BUILD_TIMEOUT" "$LUKE" BUILD "$fmtf" -o "$fbin" \
          >"$WORK/nat_f_$base.txt" 2>&1; then
        echo "FAIL $base: original native BUILD ok, FMT'd native BUILD failed" >&2
        tail -20 "$WORK/nat_f_$base.txt" >&2 || true
        exit 1
      fi
      build_count=$((build_count + 1))
    fi
    continue
  fi

  if ! timeout "$BUILD_TIMEOUT" "$LUKE" BUILD "$orig" -o "$obin" \
      >"$WORK/bo_$base.txt" 2>&1; then
    continue
  fi
  if ! timeout "$BUILD_TIMEOUT" "$LUKE" BUILD "$fmtf" -o "$fbin" \
      >"$WORK/bf_$base.txt" 2>&1; then
    echo "FAIL $base: original BUILD ok, FMT'd BUILD failed" >&2
    tail -30 "$WORK/bf_$base.txt" >&2 || true
    echo "----- FMT'd source (head) -----" >&2
    head -40 "$fmtf" >&2 || true
    exit 1
  fi
  build_count=$((build_count + 1))

  if is_serverish "$base"; then
    skip_run=$((skip_run + 1))
    continue
  fi

  set +e
  timeout "$RUN_TIMEOUT" "$obin" >"$WORK/ro_$base.txt" 2>"$WORK/roe_$base.txt"
  oc=$?
  timeout "$RUN_TIMEOUT" "$fbin" >"$WORK/rf_$base.txt" 2>"$WORK/rfe_$base.txt"
  fc=$?
  set -e

  if [[ $oc -eq 124 ]]; then
    skip_run=$((skip_run + 1))
    continue
  fi
  if [[ $fc -eq 124 ]]; then
    echo "FAIL $base: FMT'd binary hung (original exit $oc)" >&2
    exit 1
  fi
  if [[ $oc -ne "$fc" ]]; then
    echo "FAIL $base: exit codes differ (orig=$oc fmt=$fc)" >&2
    exit 1
  fi
  normalize_out "$WORK/ro_$base.txt" >"$WORK/no_$base.txt"
  normalize_out "$WORK/rf_$base.txt" >"$WORK/nf_$base.txt"
  if ! cmp -s "$WORK/no_$base.txt" "$WORK/nf_$base.txt"; then
    echo "FAIL $base: stdout differs after FMT" >&2
    diff -u "$WORK/no_$base.txt" "$WORK/nf_$base.txt" >&2 || true
    exit 1
  fi
  run_count=$((run_count + 1))
done

echo "fmt_roundtrip_ok=1 fmt=$fmt_count build=$build_count run=$run_count neg=$neg_count browser=$browser_count skip_run=$skip_run"
