#!/usr/bin/env python3
"""Exercise the whole CM5 post-install smoke control flow using fake inputs.

Never touches a real unit, protected token, CAN interface or other account.
"""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import stat
import sys
from types import SimpleNamespace
from unittest import mock


class PathFixture:
    def __init__(self, location: str):
        self.location = location

    def stat(self):
        if self.location.endswith("/token"):
            return SimpleNamespace(st_mode=stat.S_IFREG | 0o640,
                                   st_uid=0, st_gid=20)
        if self.location.endswith("/api-readouts"):
            return SimpleNamespace(st_mode=stat.S_IFDIR | 0o2750,
                                   st_uid=1000, st_gid=21)
        raise AssertionError("unexpected fixture path " + self.location)

    def is_symlink(self):
        return False

    def read_text(self, encoding):
        assert self.location.endswith("/token") and encoding == "ascii"
        return "a" * 64 + "\n"


class FakeWebConnection:
    def __init__(self, host, port, timeout):
        assert host == "127.0.0.1" and port == 8877 and timeout == 3
    def request(self, method, path):
        assert method == "GET" and path == "/"
    def getresponse(self):
        return SimpleNamespace(status=200, read=lambda: b"okay")
    def close(self):
        pass


def fake_run(*args):
    if args[:2] == ("systemctl", "is-active"):
        return "active"
    if args[:3] == ("systemctl", "show", "ecu-api-v1.service"):
        return "ecu-api" if "User" in args else "ecu-api"
    if args == ("id", "-nG", "ecu-api"):
        return "ecu-api ecu-api-read"
    if args == ("ss", "-H", "-lnt"):
        return "LISTEN 0 128 127.0.0.1:8878 0.0.0.0:*"
    if args == ("ip", "-j", "-d", "link", "show", "can0"):
        return json.dumps([{"flags": ["NOARP", "ECHO"]}])
    raise AssertionError("unexpected shell operation " + str(args))


def fake_api_request(method, path, token, extra=None):
    assert token is None or isinstance(token, str)
    if path == "/api/v1/about":
        if token is None or token == "f" * 64:
            return 401, {}, {"error": {"code": "unauthorized"}}
        if extra and extra.get("Origin") == "https://untrusted.invalid":
            return 403, {}, {"error": {"code": "origin_forbidden"}}
        if method == "POST":
            return 405, {}, {"error": {"code": "method_not_allowed"}}
        return 200, {"Cache-Control": "no-store"}, {
            "data": {"api_version": "v1", "read_only": True,
                     "build_revision": "test-revision"}}
    if path == "/api/v1/can/transmit":
        return 404, {}, {"error": {"code": "not_found"}}
    if path == "/api/v1/interfaces":
        return 200, {}, {"data": {"interfaces": [{"name": "can0", "up": False}]}}
    if path in ("/api/v1/dut/dtcs", "/api/v1/readouts/dtc/latest"):
        return 503, {}, {"error": {"code": "backend_unavailable"}}
    raise AssertionError("unexpected API request " + path)


def main():
    location = Path(__file__).resolve().parents[2] / "scripts/verify_cm5_api_v1.py"
    spec = importlib.util.spec_from_file_location("cm5_smoke_full", location)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    grp = lambda name: SimpleNamespace(gr_gid=20 if name == "ecu-api" else 21)
    pwd = lambda name: SimpleNamespace(pw_uid=1000)
    patches = (
        mock.patch.object(module, "Path", PathFixture),
        mock.patch.object(module, "run", fake_run),
        mock.patch.object(module, "api_request", fake_api_request),
        mock.patch.object(module, "reject_agent_socket", lambda user: True),
        mock.patch.object(module, "reject_token_read_as_kiosk", lambda: True),
        mock.patch.object(module, "http_client",
                          SimpleNamespace(HTTPConnection=FakeWebConnection)),
        mock.patch.object(module.os, "geteuid", lambda: 0),
        mock.patch.object(module.grp, "getgrnam", grp),
        mock.patch.object(module.pwd, "getpwnam", pwd),
        mock.patch.object(sys, "argv",
                          ["verify_cm5_api_v1.py", "--expected-revision",
                           "test-revision"]),
    )
    with contextlib.ExitStack() as stack:
        for patcher in patches:
            stack.enter_context(patcher)
        capture = io.StringIO()
        with contextlib.redirect_stdout(capture):
            module.main()
    output = capture.getvalue()
    assert "ECU_API_CM5_INSTALL_SMOKE=PASS" in output
    assert "ECU_API_DEPLOY_API_AGENT_SOCKET_DENIED=PASS" in output
    assert "ECU_API_DEPLOY_MISSING_READOUT_FAIL_CLOSED=PASS" in output
    print("ECU_API_CM5_SMOKE_MAIN=PASS")


if __name__ == "__main__":
    try:
        main()
    except Exception as err:
        print("ECU_API_CM5_SMOKE_MAIN=FAIL " + repr(err), file=sys.stderr)
        raise SystemExit(1) from None
