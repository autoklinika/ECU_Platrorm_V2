#!/usr/bin/env python3
"""Local-only negative HTTP and authentication regression; no DUT required."""
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time


def check(condition, detail):
    if not condition:
        raise AssertionError(detail)


def request(port, method, path, headers=None):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
    try:
        conn.request(method, path, headers=headers or {})
        resp = conn.getresponse()
        body = resp.read()
        return resp.status, dict(resp.getheaders()), body
    finally:
        conn.close()


def raw_request(port, payload):
    with socket.create_connection(("127.0.0.1", port), 2) as sock:
        sock.settimeout(2)
        sock.sendall(payload)
        data = sock.recv(4096)
        return int(data.split(b"\r\n", 1)[0].split()[1])


def run(binary):
    token = "a" * 64  # test-only value; never provision this on a real host
    with tempfile.TemporaryDirectory() as directory:
        token_path = Path(directory) / "token"
        token_path.write_text(token + "\n", encoding="ascii")
        if os.name != "nt":
            os.chmod(token_path, 0o600)
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        process = subprocess.Popen(
            [binary, "--token-file", str(token_path), "--port", str(port)],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
        try:
            for _ in range(75):
                if process.poll() is not None:
                    raise AssertionError("server exited before test")
                try:
                    with socket.create_connection(("127.0.0.1", port), 0.1):
                        break
                except OSError:
                    time.sleep(0.04)
            else:
                raise AssertionError("server did not listen on loopback")

            host = f"127.0.0.1:{port}"
            origin = "http://127.0.0.1:8877"
            authorized = {"Authorization": "Bearer " + token}
            status, _, _ = request(port, "GET", "/api/v1/about")
            check(status == 401, "unauthorized about")
            status, headers, body = request(port, "GET", "/api/v1/about", authorized)
            check(status == 200, "authorized about")
            check(json.loads(body)["data"]["read_only"] is True, "read only schema")
            check(headers.get("Cache-Control") == "no-store", "cache")
            status, _, body = request(port, "GET", "/api/v1/bench/session", authorized)
            check(status == 503, "unwired bench must be unavailable")
            check(json.loads(body)["error"]["code"] == "backend_unavailable",
                  "typed error")
            for path in ("/api/v1/dut", "/api/v1/dut/capabilities",
                         "/api/v1/dut/dtcs", "/api/v1/platform"):
                status, _, _ = request(port, "GET", path, authorized)
                check(status == 503, f"fail closed {path}")
            status, _, _ = request(port, "POST", "/api/v1/about", authorized)
            check(status in (400, 405), "POST blocked")
            status, _, _ = request(port, "GET", "/api/v1/can/transmit", authorized)
            check(status == 404, "CAN transmit endpoint must not exist")
            status, _, _ = request(
                port, "GET", "/api/v1/about",
                {**authorized, "Origin": "https://untrusted.invalid"})
            check(status == 403, "origin denied")
            status, headers, _ = request(
                port, "OPTIONS", "/api/v1/about",
                {"Origin": origin, "Access-Control-Request-Method": "GET",
                 "Access-Control-Request-Headers": "authorization"})
            check(status == 204 and
                  headers.get("Access-Control-Allow-Origin") == origin,
                  "preflight allowed")
            status, _, _ = request(
                port, "OPTIONS", "/api/v1/about",
                {"Origin": origin, "Access-Control-Request-Method": "POST",
                 "Access-Control-Request-Headers": "authorization"})
            check(status == 403, "write preflight denied")
            line = f"GET /api/v1/about HTTP/1.1\r\nHost: {host}\r\n".encode()
            check(raw_request(port, line + b"Authorization: Bearer " +
                              token.encode() + b"\r\nAuthorization: Bearer " +
                              token.encode() + b"\r\n\r\n") == 400,
                  "duplicate auth denied")
            check(raw_request(port, line + b"Content-Length: 0\r\n" +
                              b"Authorization: Bearer " + token.encode() +
                              b"\r\n\r\n") == 400, "unexpected body denied")
            check(raw_request(port, line + b"Transfer-Encoding: chunked\r\n" +
                              b"Authorization: Bearer " + token.encode() +
                              b"\r\n\r\n") == 400, "chunked denied")
            check(raw_request(port, line + b"\r\n") == 401, "unauth raw")
            check(raw_request(port, b"GET /api/v1/about HTTP/1.1\r\n" +
                              b"Host: evil.invalid\r\nAuthorization: Bearer " +
                              token.encode() + b"\r\n\r\n") == 403,
                  "host denied")
            check(raw_request(port, b"GET /api/v1/about?tx=1 HTTP/1.1\r\n" +
                              b"Host: " + host.encode() + b"\r\n\r\n") == 400,
                  "query blocked")
            print("ECU_API_HTTP_SECURITY=PASS")
        finally:
            process.terminate()
            try:
                process.communicate(timeout=4)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate(timeout=4)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(2)
    run(sys.argv[1])
