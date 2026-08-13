#!/usr/bin/env python3
"""Prove event-loop I/O: many half-open sockets must not stall a complete /fast.

Against http_event_idle on :8806. If each connection occupied a blocking
worker recv, 48 incomplete requests would exhaust the pool and /fast would
hang until timeout.
"""
import socket
import sys
import time

HOST, PORT = "127.0.0.1", 8806


def main():
    idle = []
    for _ in range(48):
        s = socket.create_connection((HOST, PORT), 2)
        s.sendall(b"GET /slow HTTP/1.1\r\nHost: x\r\n")  # incomplete — no final \r\n\r\n
        idle.append(s)

    t0 = time.monotonic()
    s = socket.create_connection((HOST, PORT), 2)
    s.sendall(b"GET /fast HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    buf = b""
    s.settimeout(2)
    while b"\r\n\r\n" not in buf or (b"Content-Length:" in buf and len(buf.split(b"\r\n\r\n", 1)[1]) < 4):
        chunk = s.recv(4096)
        if not chunk:
            break
        buf += chunk
    elapsed_ms = (time.monotonic() - t0) * 1000.0
    s.close()
    for x in idle:
        x.close()

    if b"fast" not in buf:
        print("event_idle_fail body=", buf[:200], file=sys.stderr)
        return 1
    if elapsed_ms > 500:
        print("event_idle_fail slow_ms=", elapsed_ms, file=sys.stderr)
        return 1
    open("/tmp/luke_event_idle_out.txt", "w").write("event_idle_ok\n")
    print("event_idle_ok ms=%.1f" % elapsed_ms)
    return 0


if __name__ == "__main__":
    sys.exit(main())
