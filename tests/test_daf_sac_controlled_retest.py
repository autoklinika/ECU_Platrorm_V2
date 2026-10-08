"""Offline-only controlled DAF SAC clear retest policy and CAN trace tests.

No CAN device, elevated privilege or ECU needed.
"""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import pty
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
AUTH = ROOT / "scripts" / "authorize_daf_sac_controlled_retest.py"
ANALYZER_PATH = ROOT / "scripts" / "analyze_daf_sac_dtc_trace.py"
spec = importlib.util.spec_from_file_location("sac_trace", ANALYZER_PATH)
assert spec and spec.loader
analyzer = importlib.util.module_from_spec(spec)
sys.modules["sac_trace"] = analyzer
spec.loader.exec_module(analyzer)


def archive_body(*, outcome: str = "UNKNOWN (NRC/TIMEOUT/ERROR)",
                 count: int = 12) -> str:
    return (
        "ECU_PLATFORM_V2_DAF_SAC_PRE_CLEAR_DTC=1\n"
        "PROFILE_ID=0xDAF00025\n"
        "REQUESTED_DTC_STATUS_MASK=0xFF\n"
        f"PRE_CLEAR_DTC_COUNT={count}\n"
        "PRE_CLEAR_DTC_EVIDENCE_COMPLETE=1\n"
        "OPERATOR_CONFIRMATION=EXPLICIT_ONE_TIME\n"
        "CLEAR_INTENT=UDS_10_03_THEN_14_FF_FF_FF\n"
        f"CLEAR_OUTCOME={outcome}\n"
    )


def make_record(directory: Path, name: str = "sac-dtc-old.txt",
                body: str | None = None) -> Path:
    record = directory / name
    record.write_text(body if body is not None else archive_body(),
                      encoding="ascii")
    record.chmod(0o600)
    return record


def check_auth(directory: Path, mode: str = "inspect") -> subprocess.CompletedProcess[str]:
    return subprocess.run([sys.executable, str(AUTH), mode, str(directory)],
                          stdin=subprocess.DEVNULL, text=True,
                          capture_output=True, timeout=5, check=False)


def candelog(can_id: int, uds: bytes, time: int) -> str:
    assert len(uds) <= 7
    payload = bytes([len(uds)]) + uds
    payload = payload.ljust(8, b"\x00")
    return f"({time}.001000) can0 {can_id:08X}#{payload.hex().upper()}\n"


def dtc_multiframe(count: int, time: int) -> str:
    payload = bytes.fromhex("59028B") + (bytes.fromhex("DFF7E98B") * count)
    length = len(payload)
    assert 7 < length < 4096
    frames = [
        bytes([0x10 | ((length >> 8) & 0x0F), length & 0xFF])
        + payload[:6],
    ]
    next_seq = 1
    for i in range(6, length, 7):
        frames.append(bytes([0x20 | next_seq]) + payload[i:i + 7])
        next_seq = (next_seq + 1) & 0x0F
    return "".join(
        f"({time + n}.001000) can0 {analyzer.RX_ID:08X}#"
        f"{f.ljust(8, bytes([0])).hex().upper()}\n"
        for n, f in enumerate(frames)
    )


def trace_summary(content: str) -> tuple[str, str]:
    with tempfile.TemporaryDirectory(prefix="sac-trace-test-") as tmp:
        filename = Path(tmp) / "candump.log"
        filename.write_text(content, encoding="ascii")
        result, classification = analyzer.summary(filename)
        return "\n".join(result), classification


class PolicyTests(unittest.TestCase):
    def test_inspect_and_ticket_consume_exactly_once(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sac-retest-policy-") as tmp:
            directory = Path(tmp)
            make_record(directory)
            result = check_auth(directory)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("PREVIOUS_DTC_COUNT=12", result.stdout)

            # The privileged wrapper MUST require a real TTY. The helper
            # also rejects a piped/non-interactive 'arm' request.
            self.assertEqual(check_auth(directory, "arm").returncode, 4)

            master, slave = pty.openpty()
            try:
                process = subprocess.Popen(
                    [sys.executable, str(AUTH), "arm", str(directory)],
                    stdin=slave, stdout=slave, stderr=subprocess.PIPE,
                    close_fds=True,
                )
                os.close(slave)
                slave = -1
                os.write(master, b"PONOW SAC 12 DTC PO UNKNOWN\n")
                _, stderr = process.communicate(timeout=5)
                self.assertEqual(process.returncode, 0, stderr)
            finally:
                if slave != -1:
                    os.close(slave)
                os.close(master)

            ticket = directory / "controlled-retest-once.txt"
            self.assertTrue(ticket.is_file())
            self.assertEqual(ticket.stat().st_mode & 0o077, 0)
            self.assertIn("STATE=CONSUMED_BEFORE_CAN_UP",
                          ticket.read_text(encoding="ascii"))
            self.assertEqual(check_auth(directory).returncode, 4)

    def test_invalid_evidence_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sac-retest-invalid-") as tmp:
            directory = Path(tmp)
            self.assertEqual(check_auth(directory).returncode, 4)
            record = make_record(directory)
            record.chmod(0o644)
            self.assertEqual(check_auth(directory).returncode, 4)
            record.chmod(0o600)

            additional = make_record(directory, "sac-dtc-extra.txt")
            self.assertEqual(check_auth(directory).returncode, 4)
            additional.unlink()

            record.write_text(archive_body(count=0), encoding="ascii")
            self.assertEqual(check_auth(directory).returncode, 4)
            record.write_text(archive_body(), encoding="ascii")

            additional = make_record(
                directory, "sac-dtc-another.txt",
                "CLEAR_INTENT=UDS_10_03_THEN_14_FF_FF_FF\n",
            )
            self.assertEqual(check_auth(directory).returncode, 4)
            additional.unlink()
            self.assertEqual(check_auth(directory).returncode, 0)

            record.unlink()
            (directory / "sac-dtc-old.txt").symlink_to("/etc/hosts")
            self.assertEqual(check_auth(directory).returncode, 4)


class TraceTests(unittest.TestCase):
    def test_complete_clear_positive_with_multiframe_dtc(self) -> None:
        content = (
            candelog(analyzer.TX_ID, bytes.fromhex("1003"), 1)
            + candelog(analyzer.RX_ID, bytes.fromhex("5003"), 2)
            + candelog(analyzer.TX_ID, bytes.fromhex("1902FF"), 3)
            + dtc_multiframe(12, 4)
            + candelog(analyzer.TX_ID, bytes.fromhex("1003"), 20)
            + candelog(analyzer.RX_ID, bytes.fromhex("5003"), 21)
            + candelog(analyzer.TX_ID, bytes.fromhex("14FFFFFF"), 22)
            + candelog(analyzer.RX_ID, bytes.fromhex("7F1478"), 23)
            + candelog(analyzer.RX_ID, bytes.fromhex("54"), 24)
            + candelog(analyzer.TX_ID, bytes.fromhex("1902FF"), 25)
            + candelog(analyzer.RX_ID, bytes.fromhex("59028B"), 26)
        )
        summary, result = trace_summary(content)
        self.assertEqual(result, "UDS_54_OBSERVED_VERIFY_POST_READ", summary)
        self.assertIn("SAC_TRACE_DTC_LIST_COUNTS=12,0", summary)
        self.assertIn("SAC_TRACE_CLEAR_REQUESTS_OBSERVED=1", summary)
        self.assertIn("SAC_TRACE_CLEAR_ACKS_OBSERVED=1", summary)
        self.assertIn("SAC_TRACE_CLEAR_RESPONSE_PENDING=1", summary)
        self.assertIn("SAC_TRACE_PARSE_ERRORS=0", summary)

    def test_nrc_and_missing_response_are_not_success(self) -> None:
        start = candelog(analyzer.TX_ID, bytes.fromhex("14FFFFFF"), 1)
        for response, expected in (
            ("7F1422", "NEGATIVE_FINAL_NRC_22"),
            ("7F1431", "NEGATIVE_FINAL_NRC_31"),
            ("7F1433", "NEGATIVE_FINAL_NRC_33"),
            ("7F1472", "NEGATIVE_FINAL_NRC_72"),
            ("7F1478", "RESPONSE_PENDING_UNRESOLVED"),
            ("", "CLEAR_REQUEST_OBSERVED_NO_RESPONSE"),
        ):
            with self.subTest(response=response):
                text = start
                if response:
                    text += candelog(analyzer.RX_ID, bytes.fromhex(response), 2)
                summary, result = trace_summary(text)
                self.assertEqual(result, expected, summary)

    def test_multiple_clear_transmits_are_reported(self) -> None:
        request = bytes.fromhex("14FFFFFF")
        text = candelog(analyzer.TX_ID, request, 1) + candelog(
            analyzer.TX_ID, request, 2)
        summary, result = trace_summary(text)
        self.assertEqual(result, "MULTIPLE_CLEAR_REQUESTS_OBSERVED", summary)

    def test_candump_spaced_format_and_truncated_transfer(self) -> None:
        spaced = (
            f"(1.0) can0 {analyzer.TX_ID:08X} [8] "
            "04 14 FF FF FF 00 00 00\n"
        )
        summary, result = trace_summary(spaced)
        self.assertEqual(result, "CLEAR_REQUEST_OBSERVED_NO_RESPONSE", summary)
        self.assertIn("SAC_TRACE_PARSE_ERRORS=0", summary)

        full = dtc_multiframe(12, 1).splitlines(keepends=True)
        summary, _ = trace_summary("".join(full[:1]))
        self.assertIn("SAC_TRACE_WARNING=INCOMPLETE_OR_MALFORMED_CAPTURE",
                      summary)


if __name__ == "__main__":
    unittest.main()
