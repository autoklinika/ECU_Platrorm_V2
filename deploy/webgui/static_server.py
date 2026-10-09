#!/usr/bin/env python3
"""Read-only, localhost-only ECU WebGUI static server. Not a diagnostic API."""
import http.client
import json
import os
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path("/opt/ecu-platform/webgui/current")
PORT = 8877
FILES = {
    "/": "index.html",
    "/index.html": "index.html",
    "/styles.css": "styles.css",
    "/src/app.mjs": "src/app.mjs",
    "/src/api-client.mjs": "src/api-client.mjs",
    "/src/sac-connect-flow.mjs": "src/sac-connect-flow.mjs",
    "/src/sac-parameter-monitor.mjs": "src/sac-parameter-monitor.mjs",
    "/src/i18n.mjs": "src/i18n.mjs",
    "/src/domain-text.mjs": "src/domain-text.mjs",
    "/src/locales/en.mjs": "src/locales/en.mjs",
    "/src/locales/pl.mjs": "src/locales/pl.mjs",
}
KIOSK_ORIGIN = "http://127.0.0.1:8877"
KIOSK_HEADER = "X-ECU-Kiosk"
READ_PROXY = {
    "/kiosk/v1/about": "/api/v1/about",
    "/kiosk/v1/interfaces": "/api/v1/interfaces",
    "/kiosk/v1/dut": "/api/v1/dut",
    "/kiosk/v1/readouts/dtc/latest": "/api/v1/readouts/dtc/latest",
    "/kiosk/v1/readouts/daf-sac/parameters/latest":
        "/api/v1/readouts/daf-sac/parameters/latest",
}
CONNECT_PROXY = "/kiosk/v1/bench/daf-sac/connect"
CONNECT_TARGET = "/api/v1/bench/daf-sac/connect"
PARAMETERS_PROXY = "/kiosk/v1/bench/daf-sac/parameters/read"
PARAMETERS_TARGET = "/api/v1/bench/daf-sac/parameters/read"
MAX_PROXY_JSON = 32768


def server_bearer():
    # systemd LoadCredential stores the existing V1 secret in a private
    # service credential directory. Never put it in static assets or JS.
    directory = os.environ.get("CREDENTIALS_DIRECTORY", "")
    if not directory.startswith("/run/credentials/"):
        raise ValueError("Kiosk service credentials unavailable")
    token = (Path(directory) / "ecu_api_token").read_text(encoding="ascii").strip()
    if not re.fullmatch(r"[0-9a-f]{64}", token):
        raise ValueError("Kiosk service credentials invalid")
    return token


CSP = (
    "default-src 'self'; script-src 'self'; style-src 'self'; "
    "connect-src 'self'; "
    "img-src 'self'; object-src 'none'; "
    "base-uri 'none'; frame-ancestors 'none'; form-action 'none'"
)


class Handler(BaseHTTPRequestHandler):
    server_version = "ECUBenchStatic/1.0"
    sys_version = ""

    def do_GET(self):
        if self.path in READ_PROXY:
            self._proxy("GET")
            return
        self._serve(include_body=True)

    def do_HEAD(self):
        self._serve(include_body=False)

    def do_POST(self):
        if self.path in (CONNECT_PROXY, PARAMETERS_PROXY):
            self._proxy("POST")
            return
        self.send_error(405, "No such kiosk operation")

    def _proxy(self, method):
        # Explicit same-origin kiosk request only; the static service has
        # no CAN capability and retains no generic shell/command endpoint.
        if (self.headers.get("Host") != "127.0.0.1:8877" or
                self.headers.get(KIOSK_HEADER) != "v1" or
                self.headers.get("Authorization") or
                self.headers.get("Transfer-Encoding") or
                self.headers.get("Content-Length") not in (None, "0") or
                (method == "POST" and self.headers.get("Origin") != KIOSK_ORIGIN)):
            self.send_error(403, "Kiosk request refused")
            return
        try:
            token = server_bearer()
            port = 8878 if method == "GET" else 8879
            path = (READ_PROXY[self.path] if method == "GET" else
                    CONNECT_TARGET if self.path == CONNECT_PROXY else PARAMETERS_TARGET)
            conn = http.client.HTTPConnection("127.0.0.1", port,
                timeout=65 if method == "POST" else 4)
            try:
                conn.request(method, path, headers={
                    "Host": f"127.0.0.1:{port}",
                    "Origin": KIOSK_ORIGIN,
                    "Accept": "application/json",
                    "Authorization": "Bearer " + token,
                    **({"Content-Length": "0"} if method == "POST" else {}),
                })
                response = conn.getresponse()
                body = response.read(MAX_PROXY_JSON + 1)
                if (len(body) > MAX_PROXY_JSON or
                        not response.getheader("Content-Type", "").lower().startswith("application/json") or
                        response.status not in (200, 400, 401, 403, 404, 409, 410, 502, 503)):
                    raise ValueError("Backend response refused")
                result = json.loads(body)
                if (not isinstance(result, dict) or result.get("schema_version") != 1 or
                        (response.status == 200 and not isinstance(result.get("data"), dict)) or
                        (response.status != 200 and not isinstance(result.get("error"), dict))):
                    raise ValueError("Backend JSON refused")
                self.send_response(response.status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self._headers()
                self.end_headers()
                self.wfile.write(body)
            finally:
                conn.close()
        except (OSError, ValueError, http.client.HTTPException):
            self.send_response(503)
            body = b'{"schema_version":1,"error":{"code":"backend_unavailable"}}'
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self._headers()
            self.end_headers()
            self.wfile.write(body)

    def _serve(self, include_body):
        path = urlsplit(self.path).path
        if path == "/favicon.ico":
            self.send_response(204)
            self._headers()
            self.end_headers()
            return
        relative = FILES.get(path)
        if relative is None:
            self.send_error(404, "Not found")
            return
        item = ROOT / relative
        try:
            data = item.read_bytes()
        except OSError:
            self.send_error(503, "Static asset unavailable")
            return
        mime = (
            "text/html; charset=utf-8" if relative.endswith(".html") else
            "text/css; charset=utf-8" if relative.endswith(".css") else
            "text/javascript; charset=utf-8"
        )
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self._headers()
        self.end_headers()
        if include_body:
            self.wfile.write(data)

    def _headers(self):
        self.send_header("Content-Security-Policy", CSP)
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Permissions-Policy",
                         "usb=(), serial=(), camera=(), microphone=(), geolocation=()")
        self.send_header("Cache-Control", "no-store")


if __name__ == "__main__":
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    server.daemon_threads = True
    server.serve_forever()
