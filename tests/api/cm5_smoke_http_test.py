#!/usr/bin/env python3
"""Execute the post-install HTTP helper, not only parse its Python source."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "scripts/verify_cm5_api_v1.py"

class StubHttpResponse:
    status = 200
    def read(self, _limit: int = -1) -> bytes:
        return json.dumps({"schema_version": 1, "data": {"api_version": "v1"}}).encode()
    def getheaders(self):
        return [("Cache-Control", "no-store")]

class StubHttpConnection:
    history: list[tuple[str, str, dict[str, str]]] = []
    def __init__(self, host: str, port: int, timeout: int):
        assert host == "127.0.0.1" and port == 8878 and timeout == 3
    def request(self, method: str, path: str, headers: dict[str, str]):
        self.history.append((method, path, headers))
    def getresponse(self):
        return StubHttpResponse()
    def close(self):
        pass

def main():
    spec = importlib.util.spec_from_file_location("cm5_api_deploy_smoke", PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    assert callable(module.api_request), "HTTP helper must be executable"
    with mock.patch.object(module.http_client, "HTTPConnection", StubHttpConnection):
        code, headers, data = module.api_request("GET", "/api/v1/about", "a" * 64)
        assert code == 200
        assert headers.get("Cache-Control") == "no-store"
        assert data["data"]["api_version"] == "v1"
        assert StubHttpConnection.history[-1] == (
            "GET", "/api/v1/about", {"Authorization": "Bearer " + "a" * 64})
        module.api_request("GET", "/api/v1/about", None)
        assert StubHttpConnection.history[-1] == ("GET", "/api/v1/about", {})
        module.api_request(
            "GET", "/api/v1/about", "a" * 64,
            {"Origin": "https://forbidden.invalid"})
        assert StubHttpConnection.history[-1][2]["Origin"] == "https://forbidden.invalid"
    print("ECU_API_CM5_SMOKE_HTTP_HELPER=PASS")

if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"ECU_API_CM5_SMOKE_HTTP_HELPER=FAIL {exc!r}", file=sys.stderr)
        raise SystemExit(1) from None
