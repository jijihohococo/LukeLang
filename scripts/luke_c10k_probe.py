#!/usr/bin/env python3
"""Keep-alive + chunked probe against http_c10k_ok on :8804."""
import re
import socket
import sys

def read_one(sock, buf):
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("eof before headers")
        buf += chunk
    h, rest = buf.split(b"\r\n\r\n", 1)
    m = re.search(br"Content-Length:\s*(\d+)", h, re.I)
    n = int(m.group(1)) if m else 0
    while len(rest) < n:
        chunk = sock.recv(4096)
        if not chunk:
            break
        rest += chunk
    body, rest = rest[:n], rest[n:]
    return h, body, rest

def main():
    s = socket.create_connection(("127.0.0.1", 8804), 2)
    s.sendall(b"GET /ka HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n")
    h1, b1, buf = read_one(s, b"")
    assert b"keep-alive" in h1.lower(), h1
    assert b1.startswith(b"ka="), b1
    s.sendall(b"GET /chunk HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    buf2 = buf
    while b"0\r\n\r\n" not in buf2:
        chunk = s.recv(4096)
        if not chunk:
            break
        buf2 += chunk
    assert b"Transfer-Encoding: chunked" in buf2, buf2[:200]
    assert b"hello " in buf2 and b"chunked" in buf2, buf2
    s.close()
    open("/tmp/luke_c10k_out.txt", "w").write("ka_chunk_ok\n")
    print("ka_chunk_ok")
    return 0

if __name__ == "__main__":
    sys.exit(main())
