"""No-hardware contract tests for the isolated SAC connection adapter."""
import importlib.util
import io
import json
from pathlib import Path
from types import SimpleNamespace
import threading
import time
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
            f"SAC_PHYSICAL_PROBE={module.identification_start(500000)}\n"
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
            f"SAC_PHYSICAL_PROBE={module.identification_start(500000)}\n"
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

    def test_real_native_start_and_pass_are_distinct_stages(self):
        output = (
            "SAC_LINK status=0 up=1 bus_off=0 bitrate=500000 fd=0\n"
            f"SAC_PHYSICAL_PROBE={module.identification_start(500000)}\n"
            "SAC_VIN_STATUS=UNPROGRAMMED_FF17\n"
            "SAC_IDENTIFICATION_COMPLETENESS=PARTIAL_NO_VIN\n"
            "SAC_SOFTWARE=2027746\n"
            "SAC_HARDWARE=K127968   \n"
            "SAC_PHYSICAL_PROBE=PASS CORE_V2_UDS_ISOTP_CAN\n"
        )
        self.assertEqual(module.decode_identity(output)["software"], "2027746")
        for corrupt in (
            output.replace(
                "SAC_PHYSICAL_PROBE=PASS CORE_V2_UDS_ISOTP_CAN",
                f"SAC_PHYSICAL_PROBE={module.identification_start(500000)}\n"
                "SAC_PHYSICAL_PROBE=PASS CORE_V2_UDS_ISOTP_CAN"),
            output + "SAC_PHYSICAL_PROBE=PASS CORE_V2_UDS_ISOTP_CAN\n",
            output.replace("bitrate=500000 mode=read-only-identification",
                           "bitrate=250000 mode=read-only-identification"),
            output.replace(f"SAC_PHYSICAL_PROBE={module.identification_start(500000)}\n", ""),
        ):
            with self.subTest(corrupt=corrupt[:90]), self.assertRaises(module.Refused):
                module.decode_identity(corrupt)

    def test_250k_and_500k_native_identity_profiles(self):
        for speed, profile in module.BITRATE_PROFILES:
            with self.subTest(bitrate=speed):
                output = (f"SAC_PHYSICAL_PROBE={module.identification_start(speed)}\n"
                    "SAC_VIN_STATUS=UNPROGRAMMED_FF17\n"
                    "SAC_IDENTIFICATION_COMPLETENESS=PARTIAL_NO_VIN\n"
                    "SAC_SOFTWARE=2027746\nSAC_HARDWARE=K127968   \n"
                    "SAC_PHYSICAL_PROBE=PASS CORE_V2_UDS_ISOTP_CAN\n")
                parsed = module.decode_identity(output, speed)
                self.assertEqual(parsed["profile_id"], profile)
                self.assertEqual(parsed["bitrate"], speed)
                with self.assertRaises(module.Refused):
                    module.decode_identity(output, 500000 if speed == 250000 else 250000)

    def test_automatic_selection_and_no_redundant_fallback(self):
        for failed_first in (False, True):
            with self.subTest(failed_first=failed_first):
                current = {"up": False, "speed": 0}
                calls = []
                def fake_ip(*args):
                    calls.append(args)
                    if args == ("link", "set", "can0", "down"):
                        current["up"] = False
                    elif args == ("link", "set", "can0", "up"):
                        current["up"] = True
                    elif args[:5] == ("link", "set", "can0", "type", "can"):
                        current["speed"] = int(args[args.index("bitrate") + 1])
                    elif args[:4] == ("-j", "-d", "link", "show"):
                        return SimpleNamespace(stdout=json.dumps([{
                            "flags": ["UP"] if current["up"] else [],
                            "linkinfo": {"info_data": {
                                "bittiming": {"bitrate": current["speed"]},
                                "state": "ERROR-ACTIVE"}}}]))
                    return SimpleNamespace(stdout="")
                probe_calls = []
                def fake_probe(path, args, operator):
                    probe_calls.append((path, args))
                    speed = int(args[1] if len(args) == 2 else args[2])
                    if len(args) == 2:
                        if speed == 250000 and failed_first:
                            raise module.Refused("communication_failed")
                        return (f"SAC_PHYSICAL_PROBE={module.identification_start(speed)}\n"
                                "SAC_VIN_STATUS=UNPROGRAMMED_FF17\n"
                                "SAC_IDENTIFICATION_COMPLETENESS=PARTIAL_NO_VIN\n"
                                "SAC_SOFTWARE=2027746\nSAC_HARDWARE=K127968   \n"
                                "SAC_PHYSICAL_PROBE=PASS CORE_V2_UDS_ISOTP_CAN\n")
                    return ("SAC_STAGE42_READ_PHYSICAL=PASS\n"
                            f"SAC_API_PARAMETERS_CAPTURED_AT_UNIX_MS={int(time.time()*1000)}\n"
                            f"SAC_API_PARAMETERS_PROFILE_ID={dict(module.BITRATE_PROFILES)[speed]}\n"
                            "SAC_API_PARAMETERS_COMPLETED_GENERATION=1\n"
                            "SAC_API_PARAMETERS_READOUT_PUBLISHED=PASS historical-completed-operation\n")
                with mock.patch.object(module, "is_up", side_effect=lambda: current["up"]), \
                     mock.patch.object(module, "ip", side_effect=fake_ip), \
                     mock.patch.object(module, "run_probe", side_effect=fake_probe):
                    result = module.connect_once(SimpleNamespace(pw_uid=1000, pw_gid=1000))
                winner = 500000 if failed_first else 250000
                self.assertEqual(result["bitrate"], winner)
                self.assertEqual(result["profile_id"], dict(module.BITRATE_PROFILES)[winner])
                self.assertNotIn("parameters_published", result)
                self.assertNotIn("parameter_completed_generation", result)
                self.assertFalse(current["up"])
                observed_speeds = [int(args[args.index("bitrate") + 1])
                    for args in calls if "bitrate" in args]
                self.assertEqual(observed_speeds,
                    [250000, 500000] if failed_first else [250000])
                self.assertEqual([args[1][1] for args in probe_calls if len(args[1]) == 2],
                    ["250000", "500000"] if failed_first else ["250000"])
                self.assertEqual([args for args in probe_calls if len(args[1]) == 4],
                                 [], "connection MUST NOT read parameters")
                if failed_first:
                    down_positions = [i for i,x in enumerate(calls)
                        if x == ("link", "set", "can0", "down")]
                    self.assertGreaterEqual(len(down_positions), 3)
                    self.assertLess(down_positions[1],
                        next(i for i,x in enumerate(calls)
                             if "bitrate" in x and "500000" in x))

    def test_parameter_provenance_rejects_old_wrong_profile_or_corrupt(self):
        floor = int(time.time() * 1000)
        profile = 0xDAF00050
        def report(ts=floor, dut=profile, generation="4"):
            return (f"SAC_STAGE42_READ_PHYSICAL=PASS\n"
                    f"SAC_API_PARAMETERS_CAPTURED_AT_UNIX_MS={ts}\n"
                    f"SAC_API_PARAMETERS_PROFILE_ID={dut}\n"
                    f"SAC_API_PARAMETERS_COMPLETED_GENERATION={generation}\n"
                    "SAC_API_PARAMETERS_READOUT_PUBLISHED=PASS historical-completed-operation\n")
        self.assertEqual(module.verified_parameter_capture(
            report(), profile, floor, floor), (floor, 4))
        for corrupt in (report(ts=floor-1), report(ts=floor+3000),
                        report(dut=0xDAF00025), report(generation="0"),
                        report(generation="-1"), report(generation="no"),
                        report().replace("SAC_API_PARAMETERS_PROFILE_ID=",
                                         "SAC_API_PARAMETERS_X="),
                        report() + "SAC_API_PARAMETERS_PROFILE_ID=3667918880\n",
                        report().replace("SAC_STAGE42_READ_PHYSICAL=PASS",
                                         "SAC_STAGE42_READ_PHYSICAL=FAIL")):
            with self.subTest(corrupt=corrupt[-95:]), self.assertRaises(module.Refused):
                module.verified_parameter_capture(corrupt, profile, floor, floor)

    def test_parameter_cycle_never_re_identifies_and_cleans_up(self):
        current = {"up": False, "speed": 0}
        calls = []
        floor = int(time.time() * 1000)

        def fake_ip(*args):
            calls.append(args)
            if args == ("link", "set", "can0", "down"):
                current["up"] = False
            elif args == ("link", "set", "can0", "up"):
                current["up"] = True
            elif "bitrate" in args:
                current["speed"] = int(args[args.index("bitrate")+1])
            return SimpleNamespace(stdout=json.dumps([{
                "flags": ["UP"] if current["up"] else [],
                "linkinfo": {"info_data": {
                    "bittiming": {"bitrate": current["speed"]},
                    "state": "ERROR-ACTIVE"}}}]))

        probes = []
        def fake_probe(path, args, operator):
            probes.append((path, args))
            assert path == module.PROBE_PARAMS
            assert args[:3] == ["can0", "parameters", "500000"]
            return ("SAC_STAGE42_READ_PHYSICAL=PASS\n"
                    f"SAC_API_PARAMETERS_CAPTURED_AT_UNIX_MS={int(time.time()*1000)}\n"
                    f"SAC_API_PARAMETERS_PROFILE_ID={0xDAF00050}\n"
                    "SAC_API_PARAMETERS_COMPLETED_GENERATION=1\n"
                    "SAC_API_PARAMETERS_READOUT_PUBLISHED=PASS historical-completed-operation\n")

        profile = 0xDAF00050
        with mock.patch.object(module, "is_up", side_effect=lambda: current["up"]), \
             mock.patch.object(module, "ip", side_effect=fake_ip), \
             mock.patch.object(module, "run_probe", side_effect=fake_probe):
            answer = module.parameters_once(SimpleNamespace(pw_uid=1000,pw_gid=1000),
                                            500000, profile)
            self.assertEqual(answer["parameters_status"], "completed")
            self.assertEqual(len(probes), 1)
            self.assertFalse(current["up"])
            self.assertEqual(current["speed"], 500000)

            def failure(_path, _args, _operator):
                raise module.Refused("communication_timeout")
            with mock.patch.object(module, "run_probe", side_effect=failure):
                timeout = module.parameters_once(
                    SimpleNamespace(pw_uid=1000,pw_gid=1000), 500000, profile)
            self.assertEqual(timeout["parameters_status"], "timeout")
            self.assertIsNone(timeout["parameter_captured_at_unix_ms"])
            self.assertFalse(current["up"])
        self.assertEqual([x for x in calls if "bitrate" in x],
                         [("link","set","can0","type","can","bitrate","500000","fd",
                           "off","listen-only","off")] * 2)

    def test_invalid_identity_aborts_without_second_bitrate(self):
        current = {"up": False, "speed": 0}
        speeds = []
        def fake_ip(*args):
            if args == ("link", "set", "can0", "down"): current["up"] = False
            if args == ("link", "set", "can0", "up"): current["up"] = True
            if "bitrate" in args:
                current["speed"] = int(args[args.index("bitrate")+1])
                speeds.append(current["speed"])
            return SimpleNamespace(stdout=json.dumps([{
                "flags": ["UP"],
                "linkinfo": {"info_data": {"state": "ERROR-ACTIVE",
                    "bittiming":{"bitrate":current["speed"]}}}}]))
        with mock.patch.object(module, "ip", side_effect=fake_ip), \
             mock.patch.object(module, "is_up", side_effect=lambda: current["up"]), \
             mock.patch.object(module, "run_probe", return_value="SAC_PHYSICAL_PROBE=PASS CORE_V2_UDS_ISOTP_CAN"):
            with self.assertRaises(module.Refused) as error:
                module.connect_once(SimpleNamespace(pw_uid=1000,pw_gid=1000))
        self.assertEqual(error.exception.reason, "invalid_identity")
        self.assertEqual(speeds, [250000])
        self.assertFalse(current["up"])

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
        with mock.patch.object(module, "is_up", return_value=False), \
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
        cls.server.active_sac = None
        cls.server.active_sac_at = 0.0
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

    def test_parameters_route_requires_connection_and_never_runs_identity(self):
        self.server.active_sac = None
        with mock.patch.object(module, "connect_once") as identify, \
             mock.patch.object(module, "parameters_once") as read:
            status, body, _ = self.call(
                "POST", path=module.SAC_PARAMETERS_PATH,
                token="Bearer " + "a"*64)
            self.assertEqual(status, 409)
            self.assertEqual(body["error"]["code"], "session_expired")
            identify.assert_not_called()
            read.assert_not_called()

        self.server.active_sac = (500000, 0xDAF00050)
        self.server.active_sac_at = time.monotonic()
        valid = {"bitrate":500000,"profile_id":0xDAF00050,
                 "parameters_published":False,"parameters_status":"unavailable",
                 "parameter_capture_floor_ms":123,
                 "parameter_captured_at_unix_ms":None,
                 "parameter_completed_generation":0}
        with mock.patch.object(module, "connect_once") as identify, \
             mock.patch.object(module, "parameters_once", return_value=valid) as read:
            status, body, _ = self.call(
                "POST", path=module.SAC_PARAMETERS_PATH,
                token="Bearer " + "a"*64)
            self.assertEqual(status, 200)
            self.assertEqual(body["data"], valid)
            identify.assert_not_called()
            read.assert_called_once()
            self.assertEqual(read.call_args.args[1:], (500000, 0xDAF00050))

        self.server.active_sac_at = time.monotonic()-module.SESSION_IDLE_SECONDS-1
        with mock.patch.object(module, "parameters_once") as read:
            status, body, _ = self.call(
                "POST", path=module.SAC_PARAMETERS_PATH,
                token="Bearer " + "a"*64)
            self.assertEqual(status, 409)
            read.assert_not_called()
        self.server.active_sac = None

    def test_authenticated_fixed_read_returns_only_validated_data(self):
        data = {"vin": None, "vin_status": "UNPROGRAMMED_FF17",
                "software": "2027746", "hardware": "K127968", "profile_id": 0xDAF00050, "bitrate": 500000,
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
