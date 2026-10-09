"""Same-origin kiosk credential proxy regression; no ECU access."""
from __future__ import annotations
import importlib.util
import json
from pathlib import Path
from threading import Thread
from types import SimpleNamespace
from urllib.request import Request, urlopen
from urllib.error import HTTPError
from http.server import ThreadingHTTPServer
from unittest import TestCase, main, mock

PATH = Path(__file__).resolve().parents[1] / "deploy/webgui/static_server.py"
spec = importlib.util.spec_from_file_location("ecu_kiosk_proxy", PATH)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class KioskProxy(TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), mod.Handler)
        cls.thread = Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.url = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)

    def call(self, path, method="GET", origin=None, badge=True, body=None):
        headers = {"Host": "127.0.0.1:8877"}
        if badge:
            headers["X-ECU-Kiosk"] = "v1"
        if origin:
            headers["Origin"] = origin
        req = Request(self.url + path, method=method, headers=headers, data=body)
        try:
            answer = urlopen(req, timeout=3)
        except HTTPError as exc:
            answer = exc
        with answer:
            return answer.status, answer.read(), answer.headers

    def test_missing_kiosk_header_forbids_proxy(self):
        status, _, _ = self.call("/kiosk/v1/about", badge=False)
        self.assertEqual(status, 403)
        status, _, _ = self.call("/kiosk/v1/bench/daf-sac/connect",
                                  method="POST", origin="http://evil.invalid")
        self.assertEqual(status, 403)

    def test_unknown_control_path_and_body_are_rejected(self):
        self.assertEqual(self.call("/kiosk/v1/bench/daf-sac/flash", method="POST")[0], 405)
        self.assertEqual(self.call("/kiosk/v1/bench/daf-sac/connect",
            method="POST", origin=mod.KIOSK_ORIGIN, body=b"actuate")[0], 403)

    def test_internal_bearer_is_not_returned_to_browser(self):
        calls = []
        class Response:
            status = 200
            def read(self, count): return json.dumps({
                "schema_version":1,
                "data":{"api_version":"v1","read_only":True,"build_revision":"rev"}}).encode()
            def getheader(self, name, default=""): return "application/json"
        class Connection:
            def __init__(self, host, port, timeout):
                calls.append(("init", host, port))
            def request(self, method, path, headers):
                calls.append((method, path, headers))
            def getresponse(self): return Response()
            def close(self): pass
        with mock.patch.object(mod, "server_bearer", return_value="a"*64), \
             mock.patch.object(mod, "http", SimpleNamespace(client=SimpleNamespace(HTTPConnection=Connection, HTTPException=mod.http.client.HTTPException))):
            status, body, headers = self.call("/kiosk/v1/about")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)["data"]["read_only"], True)
        self.assertNotIn(b"aaaaaaaaaaaa", body)
        self.assertEqual(calls[1][2]["Authorization"], "Bearer " + "a"*64)
        self.assertEqual(calls[1][1], "/api/v1/about")
        # API V1 rejects even Content-Length: 0 for GET.
        self.assertNotIn("Content-Length", calls[1][2])
        self.assertEqual(calls[0][2], 8878)

    def test_connect_uses_auth_to_existing_adapter_not_can_device(self):
        calls = []
        class Response:
            status = 503
            def read(self, count): return b'{"schema_version":1,"error":{"code":"communication_failed"}}'
            def getheader(self, name, default=""): return "application/json"
        class Connection:
            def __init__(self, host, port, timeout): calls.append(("port",port))
            def request(self, method, path, headers):
                calls.append((method,path,headers["Authorization"]))
            def getresponse(self): return Response()
            def close(self): pass
        with mock.patch.object(mod, "server_bearer", return_value="b"*64), \
             mock.patch.object(mod, "http", SimpleNamespace(client=SimpleNamespace(HTTPConnection=Connection, HTTPException=mod.http.client.HTTPException))):
            status, body, _ = self.call("/kiosk/v1/bench/daf-sac/connect",
                                         method="POST", origin=mod.KIOSK_ORIGIN)
        self.assertEqual(status,503)
        self.assertIn(b"communication_failed",body)
        self.assertEqual(calls[0],("port",8879))
        self.assertEqual(calls[1][0],"POST")
        self.assertEqual(calls[1][1],mod.CONNECT_TARGET)
        self.assertEqual(calls[1][2],"Bearer " + "b"*64)

    def test_operator_installer_denies_unprivileged_run(self):
        from subprocess import run
        cmd = str(PATH.parents[2] / "scripts/deploy_cm5_sac_kiosk_no_token.sh")
        result = run(["bash", cmd],capture_output=True,text=True,timeout=3)
        self.assertEqual(result.returncode,77)
        self.assertIn("interactive-operator-sudo-required",result.stdout)

    def test_without_system_credential_fails_closed(self):
        with mock.patch.object(mod, "server_bearer", side_effect=ValueError("denied")):
            status, body, _ = self.call("/kiosk/v1/about")
        self.assertEqual(status,503)
        self.assertNotIn(b"token",body.lower())

if __name__ == "__main__": main()
