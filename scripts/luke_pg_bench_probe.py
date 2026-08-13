#!/usr/bin/env python3
"""Concurrent /user/:id correctness against pg_api on :8811 (Postgres).

Usage:
  luke_pg_bench_probe.py [concurrency] [repeat]

Defaults: concurrency=240 (80×3 ids), workers=min(32, concurrency).
Emits pg_bench_ok and writes /tmp/luke_pg_bench_out.txt.
"""
import concurrent.futures
import socket
import sys
import time

HOST, PORT = "127.0.0.1", 8811
EXPECT = {"1": b"Ada", "2": b"Bo", "3": b"Cy"}


def one(uid: str) -> bytes:
    s = socket.create_connection((HOST, PORT), 5)
    try:
        s.sendall(
            f"GET /user/{uid} HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n".encode()
        )
        buf = b""
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            buf += chunk
            if b"\r\n\r\n" not in buf:
                continue
            h, rest = buf.split(b"\r\n\r\n", 1)
            n = 0
            for line in h.split(b"\r\n"):
                if line.lower().startswith(b"content-length:"):
                    n = int(line.split(b":", 1)[1].strip())
                    break
            while len(rest) < n:
                more = s.recv(4096)
                if not more:
                    break
                rest += more
            return rest[:n] if n else rest
        return b""
    finally:
        s.close()


def main():
    conc = int(sys.argv[1]) if len(sys.argv) > 1 else 240
    if conc < 3:
        conc = 3
    # Round up to multiple of 3 so each id is equally represented.
    nrep = (conc + 2) // 3
    ids = ["1", "2", "3"] * nrep
    ids = ids[:conc]
    workers = min(64, max(8, conc))

    for uid, name in EXPECT.items():
        body = one(uid)
        if body != name:
            print(f"pg_bench_fail want={name!r} got={body!r} uid={uid}", file=sys.stderr)
            return 1

    t0 = time.monotonic()
    errors = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
        futs = [ex.submit(one, uid) for uid in ids]
        for uid, fut in zip(ids, futs):
            try:
                body = fut.result(timeout=30)
            except Exception as e:
                print("pg_bench_fail exc", e, file=sys.stderr)
                errors += 1
                continue
            if body != EXPECT[uid]:
                print(
                    f"pg_bench_fail race want={EXPECT[uid]!r} got={body!r}",
                    file=sys.stderr,
                )
                errors += 1
    ms = (time.monotonic() - t0) * 1000.0
    if errors:
        return 1
    line = f"pg_bench_ok reqs={len(ids)} conc={conc} ms={ms:.1f}\n"
    open("/tmp/luke_pg_bench_out.txt", "w").write(line)
    print(line, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
