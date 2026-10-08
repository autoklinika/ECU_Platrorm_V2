"""Offline tests for DAF SAC clear-evidence replay protection.

No CAN, root privileges, or ECU required.
"""
from __future__ import annotations

import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
GUARD = ROOT / "scripts" / "check_daf_sac_clear_evidence.sh"


class ClearEvidenceGuardTests(unittest.TestCase):
    def run_guard(self, directory: pathlib.Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["bash", str(GUARD), str(directory)],
            text=True,
            capture_output=True,
            check=False,
            timeout=5,
        )

    def test_old_and_new_archives(self) -> None:
        cases = [
            ("empty archive", "", 0, "DTC_CLEAR_EVIDENCE_GUARD=PASS"),
            ("read with no clear intent", "PRE_CLEAR_DTC_EVIDENCE_COMPLETE=1\n", 0,
             "DTC_CLEAR_EVIDENCE_GUARD=PASS"),
            ("confirmation rejected", "OPERATOR_CONFIRMATION=REJECTED\n", 0,
             "DTC_CLEAR_EVIDENCE_GUARD=PASS"),
            ("old unknown result", "CLEAR_OUTCOME=UNKNOWN (NRC/TIMEOUT/ERROR)\n", 4,
             "DTC_CLEAR_BLOCKED=PREVIOUS_OUTCOME_UNKNOWN"),
            ("interrupted old runner", "CLEAR_INTENT=UDS_10_03_THEN_14_FF_FF_FF\n", 4,
             "DTC_CLEAR_BLOCKED=PREVIOUS_OUTCOME_UNVERIFIED"),
            ("interrupted new runner", "CLEAR_ATTEMPT=UNRESOLVED_UNTIL_VERIFIED\n", 4,
             "DTC_CLEAR_BLOCKED=PREVIOUS_OUTCOME_UNVERIFIED"),
            ("positive ACK, failed reread",
             "CLEAR_INTENT=UDS_10_03_THEN_14_FF_FF_FF\nCLEAR_UDS_54_ACK=YES\n",
             4, "DTC_CLEAR_BLOCKED=PREVIOUS_OUTCOME_UNVERIFIED"),
            ("verified known result",
             "CLEAR_ATTEMPT=UNRESOLVED_UNTIL_VERIFIED\n"
             "CLEAR_INTENT=UDS_10_03_THEN_14_FF_FF_FF\n"
             "CLEAR_UDS_54_ACK=YES\n"
             "POST_CLEAR_VERIFICATION=READ_COMPLETED\n",
             0, "DTC_CLEAR_EVIDENCE_GUARD=PASS"),
            ("old UNKNOWN may not be masked by later claimed ACK",
             "CLEAR_OUTCOME=UNKNOWN (NRC/TIMEOUT/ERROR)\n"
             "CLEAR_UDS_54_ACK=YES\nPOST_CLEAR_VERIFICATION=READ_COMPLETED\n",
             4, "DTC_CLEAR_BLOCKED=PREVIOUS_OUTCOME_UNKNOWN"),
        ]
        with tempfile.TemporaryDirectory(prefix="sac-dtc-guard-") as tmp:
            directory = pathlib.Path(tmp)
            archive = directory / "sac-dtc-example.txt"
            for name, body, expected_rc, expected_message in cases:
                with self.subTest(case=name):
                    if archive.exists():
                        archive.unlink()
                    if body:
                        archive.write_text(body, encoding="utf-8")
                    result = self.run_guard(directory)
                    self.assertEqual(result.returncode, expected_rc, result)
                    self.assertIn(expected_message, result.stdout + result.stderr)

    def test_rejects_symlinked_evidence(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sac-dtc-guard-") as tmp:
            directory = pathlib.Path(tmp)
            (directory / "target.txt").write_text("PRE_CLEAR_DTC_EVIDENCE_COMPLETE=1\n")
            (directory / "sac-dtc-injected.txt").symlink_to(directory / "target.txt")
            result = self.run_guard(directory)
            self.assertEqual(result.returncode, 4, result)
            self.assertIn("DTC_CLEAR_BLOCKED=INVALID_PREVIOUS_EVIDENCE",
                          result.stderr)

    def test_rejects_missing_directory(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sac-dtc-guard-") as tmp:
            result = self.run_guard(pathlib.Path(tmp) / "nonexistent")
            self.assertEqual(result.returncode, 4, result)


if __name__ == "__main__":
    unittest.main()
