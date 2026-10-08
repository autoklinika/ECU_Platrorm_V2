#!/usr/bin/env python3
"""Offline CAN/ISO-TP/UDS evidence analyzer for the controlled SAC clear retest.

Reads candump text only. Never opens CAN, never transmits or retries. A missing
captured response is NOT proof that the ECU rejected or ignored UDS 0x14.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import re
import sys

TX_ID = 0x18DA30F9
RX_ID = 0x18DAF930
LINE_HASH = re.compile(
    r"^\(([^)]+)\)\s+\S+\s+([0-9A-Fa-f]{3,8})#([0-9A-Fa-f]+)"
)
LINE_SPACED = re.compile(
    r"^\(([^)]+)\)\s+\S+\s+([0-9A-Fa-f]{3,8})\s+"
    r"\[(\d+)\]\s+((?:[0-9A-Fa-f]{2}\s*)+)"
)

NRC = {
    0x10: "generalReject",
    0x11: "serviceNotSupported",
    0x12: "subFunctionNotSupported",
    0x13: "incorrectMessageLengthOrInvalidFormat",
    0x22: "conditionsNotCorrect",
    0x31: "requestOutOfRange",
    0x33: "securityAccessDenied",
    0x72: "generalProgrammingFailure",
    0x78: "responsePending",
    0x7E: "subFunctionNotSupportedInActiveSession",
    0x7F: "serviceNotSupportedInActiveSession",
}


@dataclass
class Reassembly:
    expected: int = 0
    sequence: int = 1
    data: bytearray = field(default_factory=bytearray)

    def take(self, frame: bytes) -> tuple[bytes | None, bool]:
        if not frame:
            return None, True
        kind = frame[0] >> 4
        if kind == 0:  # Classic CAN single frame, 0 to 7 UDS data bytes
            self.expected = 0
            self.data.clear()
            length = frame[0] & 0x0F
            if length == 0 or length > len(frame) - 1:
                return None, True
            return bytes(frame[1:1 + length]), False
        if kind == 1:
            if len(frame) < 3:
                return None, True
            self.expected = ((frame[0] & 0x0F) << 8) | frame[1]
            self.sequence = 1
            self.data = bytearray(frame[2:])
            if self.expected <= len(self.data) or self.expected > 4095:
                return None, True
            return None, False
        if kind == 2:
            if self.expected == 0 or (frame[0] & 0x0F) != self.sequence:
                self.expected = 0
                self.data.clear()
                return None, True
            self.data.extend(frame[1:])
            self.sequence = (self.sequence + 1) & 0x0F
            if len(self.data) >= self.expected:
                message = bytes(self.data[:self.expected])
                self.expected = 0
                self.data.clear()
                return message, False
            return None, False
        if kind == 3:  # FlowControl, not a UDS PDU
            return None, False
        return None, True


def parse_frame(line: str) -> tuple[str, int, bytes] | None:
    found = LINE_HASH.match(line)
    if found:
        stamp, can_id, octets = found.groups()
        if len(octets) % 2:
            return None
        return stamp, int(can_id, 16), bytes.fromhex(octets)
    found = LINE_SPACED.match(line)
    if found:
        stamp, can_id, length, octets = found.groups()
        data = bytes.fromhex(octets)
        if len(data) != int(length):
            return None
        return stamp, int(can_id, 16), data
    return None


def summary(path: Path) -> tuple[list[str], str]:
    lines: list[str] = ["SAC_TRACE_ANALYZER=OFFLINE_NO_TX"]
    tx = Reassembly()
    rx = Reassembly()
    count_frames = 0
    errors = 0
    clear_requests = 0
    positive = 0
    negative_final: list[int] = []
    pending = 0
    dtc_counts: list[int] = []
    capture_started_clear = False

    try:
        source = path.open(encoding="ascii", errors="replace")
    except OSError:
        return lines + ["SAC_TRACE_RESULT=UNREADABLE_CAPTURE"], "UNREADABLE_CAPTURE"
    with source:
        for line in source:
            parsed = parse_frame(line.strip())
            if parsed is None:
                if line.strip():
                    errors += 1
                continue
            stamp, identifier, frame = parsed
            if identifier not in (TX_ID, RX_ID):
                continue
            count_frames += 1
            is_tx = identifier == TX_ID
            msg, malformed = (tx if is_tx else rx).take(frame)
            if malformed:
                errors += 1
            if msg is None:
                continue
            label = "TX" if is_tx else "RX"
            sid = msg[0]
            if is_tx and sid == 0x10 and msg == bytes.fromhex("1003"):
                lines.append(f"SAC_TRACE_EVENT={stamp} {label} UDS_10_03")
            elif is_tx and sid == 0x19 and msg[:2] == bytes.fromhex("1902"):
                lines.append(f"SAC_TRACE_EVENT={stamp} {label} UDS_19_02 mask=0x{msg[2]:02X}"
                             if len(msg) == 3 else
                             f"SAC_TRACE_EVENT={stamp} {label} MALFORMED_19")
            elif is_tx and sid == 0x14:
                clear_requests += 1
                capture_started_clear = True
                lines.append(f"SAC_TRACE_EVENT={stamp} {label} UDS_14_{msg.hex().upper()}")
            elif not is_tx and sid == 0x54:
                if capture_started_clear:
                    positive += 1
                lines.append(f"SAC_TRACE_EVENT={stamp} {label} UDS_54_ACK")
            elif not is_tx and sid == 0x7F and len(msg) == 3:
                requested_sid = msg[1]
                nrc = msg[2]
                lines.append(
                    f"SAC_TRACE_EVENT={stamp} {label} UDS_NRC "
                    f"sid=0x{requested_sid:02X} code=0x{nrc:02X} "
                    f"name={NRC.get(nrc, 'other')}"
                )
                if requested_sid == 0x14 and capture_started_clear:
                    if nrc == 0x78:
                        pending += 1
                    else:
                        negative_final.append(nrc)
            elif not is_tx and sid == 0x59 and len(msg) >= 3 and msg[1] == 0x02:
                if (len(msg) - 3) % 4 == 0:
                    count = (len(msg) - 3) // 4
                    dtc_counts.append(count)
                    lines.append(f"SAC_TRACE_EVENT={stamp} {label} UDS_59_02 "
                                 f"dtc_count={count} availability=0x{msg[2]:02X}")
                else:
                    errors += 1
                    lines.append(f"SAC_TRACE_EVENT={stamp} {label} MALFORMED_DTC_LIST")
            elif not is_tx and sid == 0x50:
                lines.append(f"SAC_TRACE_EVENT={stamp} {label} UDS_50_SESSION")
    if tx.expected or rx.expected:
        errors += 1  # incomplete multi-frame capture is never proof of success

    if clear_requests == 0:
        result = "NO_CLEAR_REQUEST_OBSERVED"
    elif clear_requests != 1:
        result = "MULTIPLE_CLEAR_REQUESTS_OBSERVED"
    elif positive and negative_final:
        result = "CONFLICTING_CLEAR_REPLIES"
    elif positive == 1:
        result = "UDS_54_OBSERVED_VERIFY_POST_READ"
    elif positive > 1:
        result = "MULTIPLE_CLEAR_ACKS_OBSERVED"
    elif negative_final:
        result = "NEGATIVE_FINAL_NRC_" + "_".join(f"{v:02X}" for v in negative_final)
    elif pending:
        result = "RESPONSE_PENDING_UNRESOLVED"
    else:
        result = "CLEAR_REQUEST_OBSERVED_NO_RESPONSE"

    lines.extend([
        f"SAC_TRACE_FRAMES={count_frames}",
        f"SAC_TRACE_PARSE_ERRORS={errors}",
        f"SAC_TRACE_CLEAR_REQUESTS_OBSERVED={clear_requests}",
        f"SAC_TRACE_CLEAR_ACKS_OBSERVED={positive}",
        f"SAC_TRACE_CLEAR_RESPONSE_PENDING={pending}",
        f"SAC_TRACE_DTC_LIST_COUNTS={','.join(str(c) for c in dtc_counts)}",
        f"SAC_TRACE_RESULT={result}",
    ])
    if errors:
        lines.append("SAC_TRACE_WARNING=INCOMPLETE_OR_MALFORMED_CAPTURE")
    lines.append("SAC_TRACE_NOTE=CAN_CAPTURE_ALONE_CANNOT_PROVE_ZERO_REMAINING_DTC")
    return lines, result


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: analyze_daf_sac_dtc_trace.py <candump.log>", file=sys.stderr)
        return 2
    lines, result = summary(Path(sys.argv[1]))
    print("\n".join(lines))
    return 0 if result != "UNREADABLE_CAPTURE" else 1


if __name__ == "__main__":
    sys.exit(main())
