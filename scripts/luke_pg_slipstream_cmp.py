#!/usr/bin/env python3
"""Compare blocking vs Slipstream (LUKE_PG_ASYNC=1) at several concurrencies.

Expects pg_api already built at vm/build/pg_api. Starts the server twice
(with env), runs luke_pg_bench_probe.py, prints ms. Optional: --latency-ms N
injects netem delay on lo (requires root + tc).

Exit 0 if Slipstream correctness passes at every level; prints a WARN if
Slipstream is slower than blocking on localhost (expected until draft path
is warm — acceptance still requires ≥ blocking within noise before default).
"""
from __future__ import annotations

import argparse
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PG_API = ROOT / "vm" / "build" / "pg_api"
PROBE = ROOT / "scripts" / "luke_pg_bench_probe.py"
LOG = Path("/tmp/luke_pg_slipstream_cmp.log")


def run_once(label: str, env: dict, conc: int) -> float | None:
    for port_kill in ("8811",):
        subprocess.run(["fuser", "-k", f"{port_kill}/tcp"], capture_output=True)
    time.sleep(0.2)
    logf = open(LOG, "w")
    proc = subprocess.Popen(
        [str(PG_API)],
        stdout=logf,
        stderr=subprocess.STDOUT,
        env=env,
        cwd=str(ROOT / "vm"),
    )
    try:
        time.sleep(0.7)
        if proc.poll() is not None:
            print(f"fail {label}: server exited early", file=sys.stderr)
            return None
        out = subprocess.check_output(
            [sys.executable, str(PROBE), str(conc)], text=True, timeout=60
        )
        # pg_bench_ok reqs=N conc=C ms=M
        ms = None
        for part in out.strip().split():
            if part.startswith("ms="):
                ms = float(part[3:])
        print(f"{label} conc={conc} {out.strip()}")
        return ms
    except Exception as e:
        print(f"fail {label} conc={conc}: {e}", file=sys.stderr)
        return None
    finally:
        try:
            proc.send_signal(signal.SIGTERM)
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
        logf.close()
        subprocess.run(["fuser", "-k", "8811/tcp"], capture_output=True)


def netem(delay_ms: int | None, clear: bool = False) -> None:
    if clear:
        subprocess.run(
            ["tc", "qdisc", "del", "dev", "lo", "root"], capture_output=True
        )
        return
    if delay_ms is None:
        return
    subprocess.run(["tc", "qdisc", "del", "dev", "lo", "root"], capture_output=True)
    r = subprocess.run(
        [
            "tc",
            "qdisc",
            "add",
            "dev",
            "lo",
            "root",
            "netem",
            "delay",
            f"{delay_ms}ms",
        ],
        capture_output=True,
        text=True,
    )
    if r.returncode != 0:
        print(f"warn: tc netem unavailable: {r.stderr.strip()}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--latency-ms", type=int, default=0)
    ap.add_argument("--conc", default="50,200,500")
    args = ap.parse_args()
    if not PG_API.is_file():
        print("missing vm/build/pg_api — build pg_api.luke first", file=sys.stderr)
        return 1

    concs = [int(x) for x in args.conc.split(",") if x.strip()]
    base = os.environ.copy()
    delay = args.latency_ms if args.latency_ms > 0 else None
    if delay:
        netem(delay)
    try:
        ok = True
        for c in concs:
            env_b = base.copy()
            env_b["LUKE_PG_ASYNC"] = "0"
            env_s = base.copy()
            env_s["LUKE_PG_ASYNC"] = "1"
            mb = run_once("blocking", env_b, c)
            ms = run_once("slipstream", env_s, c)
            if mb is None or ms is None:
                ok = False
                continue
            if delay:
                if ms >= mb:
                    print(
                        f"WARN latency: slipstream {ms:.1f}ms not faster than blocking {mb:.1f}ms",
                        file=sys.stderr,
                    )
                else:
                    print(f"latency win: slipstream {ms:.1f}ms < blocking {mb:.1f}ms")
            else:
                # Localhost: Slipstream should track blocking (within ~25% noise).
                if ms > mb * 1.25 + 5:
                    print(
                        f"WARN localhost: slipstream {ms:.1f}ms slower than blocking {mb:.1f}ms",
                        file=sys.stderr,
                    )
        return 0 if ok else 1
    finally:
        if delay:
            netem(None, clear=True)


if __name__ == "__main__":
    sys.exit(main())
