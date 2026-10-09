"""Offline contract tests for the operator-only Issue #29 physical verifier."""
import importlib.util
from pathlib import Path
import unittest

MODULE = Path(__file__).resolve().parents[1] / "scripts/verify_cm5_sac_issue29_physical.py"
spec = importlib.util.spec_from_file_location("issue29_capture_check", MODULE)
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


class Issue29CaptureContract(unittest.TestCase):
    def sample(self):
        identity = {
            "parameters_published": True, "parameters_status": "completed",
            "profile_id": 0xDAF00025, "bitrate": 250000,
            "parameter_captured_at_unix_ms": 1791545568082,
            "parameter_completed_generation": 1,
        }
        capture = {
            "source": "completed_application_operation", "live": False,
            "captured_at_unix_ms": 1791545568082,
            "profile_id": 0xDAF00025, "completed_generation": 1,
            "parameters": {
                "permanent_voltage_v": 22.3, "ignition_voltage_v": 22.3,
                "pgn_feae_observed": True,
                "pressure1_bar": None, "pressure2_bar": None,
            },
        }
        return identity, capture

    def test_realistic_250k_with_pressure_unavailable(self):
        identity, capture = self.sample()
        result = verifier.check_capture(identity, capture)
        self.assertIsNone(result["pressure1_bar"])
        self.assertIsNone(result["pressure2_bar"])

    def test_500k_is_valid_with_matching_profile(self):
        identity, capture = self.sample()
        identity["bitrate"] = 500000
        identity["profile_id"] = capture["profile_id"] = 0xDAF00050
        verifier.check_capture(identity, capture)

    def test_wrong_snapshot_and_wrong_generation_rejected(self):
        for field, delta in (("captured_at_unix_ms", 1),
                             ("completed_generation", 1),
                             ("profile_id", 1)):
            identity, capture = self.sample()
            capture[field] += delta
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                verifier.check_capture(identity, capture)

    def test_identification_without_new_measurement_is_not_success(self):
        identity, capture = self.sample()
        identity["parameters_status"] = "timeout"
        with self.assertRaises(RuntimeError):
            verifier.check_capture(identity, capture)

    def test_fe96_out_of_range_and_invalid_feae_rejected(self):
        for name, value in (("permanent_voltage_v", 70.0),
                            ("ignition_voltage_v", -1.0),
                            ("pressure1_bar", 123.0),
                            ("pressure2_bar", -1.0)):
            identity, capture = self.sample()
            capture["parameters"][name] = value
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                verifier.check_capture(identity, capture)


if __name__ == "__main__":
    unittest.main()
