"""No-hardware contract tests for the isolated SAC connection adapter."""
import importlib.util
import io
import json
from pathlib import Path
from types import SimpleNamespace
import threading
import unittest
from unittest import mock
from urllib import request, error

SERVER_FILE = Path(__file__).resolve().parents[1] / "deploy/sac_connect/sac_identify_server.py"
spec = importlib.util.spec_from_file_location("ecu_sac_connect", SERVER_FILE)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ParserSafety(unittest.TestCase):
    def test_known_500k_ff17_is_not_a_fabricated_vin(self):
        answer = module.decode_identity(
            "SAC_VIN_STATUS=UNPROGRAMMED_FF17\n"
            "SAC_IDENTIFICATION_COMPLETENESS=PARTIAL_NO_VIN\n"
            "SAC_SOFTWARE=2027746\nSAC_HARDWARE=K127968   \n"
            "SAC_PHYSICAL_PROBE=PASS CORE_V2_UDS_ISOTP_CAN\n")
        self.assertIsNone(answer["vin"])
        self.assertEqual(answer["vin_status"], "UNPROGRAMMED_FF17")
        self.assertEqual(answer["software"], "2027746")
        self.assertEqual(answer["hardware"], "K127968")

    def test_valid_vin_and_invalid_markers(self):
        answer = module.decode_identity(
            "SAC_VIN_STATUS=VALID_ASCII\nSAC_VIN=WAUZZZ8V0JA123456\n"
            "SAC_SOFTWARE=123\nSAC_HARDWARE=ABC\n"
            "SAC_PHYSICAL_PROBE=PASS CORE_V2_UDS_ISOTP_CAN")
        self.assertEqual(answer["vin"], "WAUZZZ8V0JA123456")
        for bad in [
            "SAC_VIN_STATUS=VALID_ASCII\nSAC_VIN=SHORT\n"
            "SAC_SOFTWARE=SW\nSAC_HARDWARE=HW\n"
            "SAC_PHYSICAL_PROBE=PASS CORE_V2_UDS_ISOTP_CAN",
            "SAC_VIN_STATUS=UNPROGRAMMED_FF17\nSAC_SOFTWARE=SW\n"
            "SAC_HARDWARE=HW\nSAC_PHYSICAL_PROBE=PASS CORE_V2_UDS_ISOTP_CAN",
            "SAC_VIN_STATUS=VALID_ASCII\nSAC_VIN=WAUZZZ8V0JA123456\n"
            "SAC_SOFTWARE=SW\nSAC_HARDWARE=HW\nSAC_PHYSICAL_PROBE=FAIL",
            "SAC_VIN_STATUS=UNPROGRAMMED_FF17\n"
            "SAC_IDENTIFICATION_COMPLETENESS=PARTIAL_NO_VIN\n"
            "SAC_SOFTWARE=SW\nSAC_SOFTWARE=SW2\nSAC_HARDWARE=HW\n"
            "SAC_PHYSICAL_PROBE=PASS CORE_V2_UDS_ISOTP_CAN",
        ]:
            with self.subTest(bad=bad[:45]), self.assertRaises(module.Refused):
                module.decode_identity(bad)

    def test_busy_refuses_without_configuring_can(self):
        with mock.patch.object(module, "is_up", return_value=True), \
             mock.patch.object(module, "ip") as ip:
            with self.assertRaises(module.Refused) as error_info:
                module.connect_once(SimpleNamespace(pw_uid=1000, pw_gid=1000))
            self.assertEqual(error_info.exception.reason, "bench_busy")
            ip.assert_not_called()

    def test_cleanup_even_after_probe_rejection(self):
        info = type("Response", (), {
            "stdout": json.dumps([{"flags": ["UP"],
                                   "linkinfo": {"info_data": {
                                       "bittiming": {"bitrate": 500000},
                                       "state": "ERROR-ACTIVE",
                                       "listen-only": False}}}])})
        with mock.patch.object(module, "is_up", side_effect=[False, False]), \
             mock.patch.object(module, "ip", return_value=info) as ip, \
             mock.patch.object(module, "run_probe", side_effect=module.Refused("communication_failed")):
            with self.assertRaises(module.Refused):
                module.connect_once(SimpleNamespace(pw_uid=1000, pw_gid=1000))
            self.assertEqual(ip.call_args.args, ("link", "set", "can0", "down"))


class HttpSafety(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = module.ThreadingHTTPServer(("127.0.0.1", 0), module.Handler)
        cls.server.token = "a" * 64
        cls.server.operator = SimpleNamespace(pw_uid=1000, pw_gid=1000)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.address = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=3)

    def call(self, method, origin=module.ORIGIN, token=None, path=module.SAC_CONNECT_PATH,
             headers=None, data=None):
        hdr = {"Origin": origin, "Host": f"{module.HOST}:{module.PORT}"}
        hdr.update(headers or {})
        if token is not None:
            hdr["Authorization"] = token
        req = request.Request(self.address + path, method=method, headers=hdr, data=data)
        try:
            res = request.urlopen(req, timeout=2)
        except error.HTTPError as exc:
            res = exc
        with res:
            raw = res.read()
            return res.status, json.loads(raw) if raw else None, res.headers

    def test_missing_token_is_not_a_physical_request(self):
        with mock.patch.object(module, "connect_once") as physical:
            status, body, hdr = self.call("POST", token=None, headers={"Content-Length": "0"})
            self.assertEqual(status, 401)
            self.assertEqual(body["error"]["code"], "unauthorized")
            physical.assert_not_called()

    def test_wrong_origin_and_body_rejected(self):
        with mock.patch.object(module, "connect_once") as physical:
            self.assertEqual(self.call("POST", origin="http://evil.example",
                                       token="Bearer " + "a" * 64)[0], 403)
            self.assertEqual(self.call("POST", token="Bearer " + "a" * 64,
                                       data=b"actuation")[0], 400)
            physical.assert_not_called()

    def test_cors_fixed_path_and_methods(self):
        with mock.patch.object(module, "connect_once") as physical:
            status, _, _ = self.call("POST", token="Bearer " + "a" * 64,
                                    path="/api/v1/bench/daf-sac/flash")
            self.assertEqual(status, 403)
            status, _, _ = self.call("GET", token="Bearer " + "a" * 64)
            self.assertEqual(status, 405)
            req = request.Request(self.address + module.SAC_CONNECT_PATH,
                                  method="OPTIONS", headers={
                                      "Origin": module.ORIGIN,
                                      "Host": f"{module.HOST}:{module.PORT}",
                                      "Access-Control-Request-Method": "POST",
                                      "Access-Control-Request-Headers": "authorization"})
            with request.urlopen(req, timeout=2) as result:
                self.assertEqual(result.status, 204)
                self.assertEqual(result.headers["Access-Control-Allow-Origin"], module.ORIGIN)
            physical.assert_not_called()

    def test_authenticated_fixed_read_returns_only_validated_data(self):
        data = {"vin": None, "vin_status": "UNPROGRAMMED_FF17",
                "software": "2027746", "hardware": "K127968", "profile_id": module.PROFILE,
                "parameters_published": True, "parameter_capture_floor_ms": 100}
        with mock.patch.object(module, "connect_once", return_value=data) as physical:
            status, body, headers = self.call("POST", token="Bearer " + "a" * 64,
                                             headers={"Content-Length": "0"})
            self.assertEqual(status, 200)
            self.assertEqual(body["data"], data)
            self.assertEqual(headers["Cache-Control"], "no-store")
            physical.assert_called_once()

if __name__ == "__main__":
    unittest.main()
