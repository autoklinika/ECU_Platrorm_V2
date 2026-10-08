#!/usr/bin/env python3
"""Read-only, localhost-only ECU WebGUI static server. Not a diagnostic API."""
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
    "/src/i18n.mjs": "src/i18n.mjs",
    "/src/domain-text.mjs": "src/domain-text.mjs",
    "/src/locales/en.mjs": "src/locales/en.mjs",
    "/src/locales/pl.mjs": "src/locales/pl.mjs",
}
CSP = (
    "default-src 'self'; script-src 'self'; style-src 'self'; "
    "connect-src 'self' http://127.0.0.1:8878; img-src 'self'; object-src 'none'; "
    "base-uri 'none'; frame-ancestors 'none'; form-action 'none'"
)


class Handler(BaseHTTPRequestHandler):
    server_version = "ECUBenchStatic/1.0"
    sys_version = ""

    def do_GET(self):
        self._serve(include_body=True)

    def do_HEAD(self):
        self._serve(include_body=False)

    def do_POST(self):
        self.send_error(405, "Read-only static host")

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
