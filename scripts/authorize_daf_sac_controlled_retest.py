#!/usr/bin/env python3
"""One-shot operator authorization for an UNKNOWN DAF SAC DTC clear retest.

This module NEVER opens CAN or sends UDS frames. It does not alter the
previous evidence. The only persistent mutation is an exclusive, durable
one-shot ticket that must exist before the supervised retest activates CAN.
"""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import select
import stat
import sys
import time

TICKET = "controlled-retest-once.txt"
PROFILE = "0xDAF00025"


def fail(reason: str) -> None:
    print(f"SAC_CONTROLLED_RETEST=DENIED reason={reason}", file=sys.stderr)
    raise SystemExit(4)


def private_path(path: Path, *, directory: bool) -> None:
    info = path.lstat()
    if (not stat.S_ISDIR(info.st_mode) if directory
            else not stat.S_ISREG(info.st_mode)):
        fail("invalid-evidence-file-type")
    if info.st_uid != os.geteuid() or info.st_mode & 0o077:
        fail("unsafe-evidence-ownership-or-permissions")


def inspect(folder: Path) -> tuple[Path, str, int]:
    if not folder.is_absolute() or not folder.exists():
        fail("invalid-evidence-directory")
    private_path(folder, directory=True)
    if (folder / TICKET).exists() or (folder / TICKET).is_symlink():
        fail("retest-ticket-already-consumed")
    records = sorted(folder.glob("sac-dtc-*.txt"))
    if not records:
        fail("no-previous-evidence")
    unknown: list[tuple[Path, bytes, int]] = []
    for record in records:
        private_path(record, directory=False)
        raw = record.read_bytes()
        # Verify all previous attempts before permitting a special exception.
        data = raw.decode("ascii", errors="strict")
        lines = set(data.splitlines())
        has_intent = any(s.startswith(("CLEAR_INTENT=", "CLEAR_ATTEMPT="))
                         for s in lines)
        has_unknown = any(s.startswith("CLEAR_OUTCOME=UNKNOWN") for s in lines)
        fully_verified = (
            "CLEAR_UDS_54_ACK=YES" in lines
            and "POST_CLEAR_VERIFICATION=READ_COMPLETED" in lines
        )
        if has_unknown:
            if not has_intent or fully_verified:
                fail("inconsistent-previous-outcome")
            match = re.search(r"^PRE_CLEAR_DTC_COUNT=([0-9]+)$",
                              data, re.MULTILINE)
            if not match or not (1 <= int(match.group(1)) <= 128):
                fail("invalid-previous-dtc-count")
            expected = {
                "ECU_PLATFORM_V2_DAF_SAC_PRE_CLEAR_DTC=1",
                "PRE_CLEAR_DTC_EVIDENCE_COMPLETE=1",
                "REQUESTED_DTC_STATUS_MASK=0xFF",
                f"PROFILE_ID={PROFILE}",
                "OPERATOR_CONFIRMATION=EXPLICIT_ONE_TIME",
                "CLEAR_INTENT=UDS_10_03_THEN_14_FF_FF_FF",
            }
            if not expected.issubset(lines):
                fail("incomplete-previous-evidence")
            unknown.append((record, raw, int(match.group(1))))
        elif has_intent and not fully_verified:
            fail("another-unresolved-attempt")
    if len(unknown) != 1:
        fail("expected-exactly-one-unknown-attempt")
    record, body, count = unknown[0]
    return record, hashlib.sha256(body).hexdigest(), count


def consume(folder: Path, record: Path, digest: str, count: int) -> None:
    if not (sys.stdin.isatty() and sys.stdout.isatty()):
        fail("interactive-tty-required")
    phrase = f"PONOW SAC {count} DTC PO UNKNOWN"
    print()
    print("UWAGA: kontrolowana, jednorazowa i nieodwracalna proba na fizycznym SAC.")
    print("Poprzednie kasowanie pozostaje UNKNOWN i nie jest anulowane.")
    print(f"Poprzednie archiwum: {record.name}")
    print(f"SHA256 archiwum: {digest}")
    print("Ten etap rezerwuje JEDYNA probe, nawet gdy program zostanie przerwany.")
    print("Po rezerwacji aplikacja ponownie odczyta DTC i poprosi o kolejne")
    print("potwierdzenie bezposrednio przed wyslaniem UDS 0x14.")
    print(f"Wpisz dokladnie: {phrase}")
    print("> ", end="", flush=True)
    readable, _, _ = select.select([sys.stdin], [], [], 120)
    if not readable or sys.stdin.readline().rstrip("\r\n") != phrase:
        fail("operator-declined-or-timeout")

    # Re-evaluate after consent: if files changed, do not issue the ticket.
    new_record, new_digest, new_count = inspect(folder)
    if (new_record != record or new_digest != digest or new_count != count):
        fail("evidence-changed-during-confirmation")
    ticket = folder / TICKET
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW
    try:
        fd = os.open(ticket, flags, 0o600)
    except OSError as exc:
        fail(f"ticket-create-failed:{exc.errno}")
    try:
        text = (
            "ECU_PLATFORM_V2_SAC_CONTROLLED_RETEST=1\n"
            f"PREVIOUS_ARCHIVE={record.name}\n"
            f"PREVIOUS_ARCHIVE_SHA256={digest}\n"
            f"PREVIOUS_PRE_CLEAR_DTC_COUNT={count}\n"
            "PREVIOUS_OUTCOME=UNKNOWN\n"
            "OPERATOR_AUTHORIZATION=EXPLICIT_TTY\n"
            "STATE=CONSUMED_BEFORE_CAN_UP\n"
            f"UTC_UNIX_TIME={int(time.time())}\n"
        ).encode("ascii")
        with os.fdopen(fd, "wb", closefd=True) as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        parent_fd = os.open(folder, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        try:
            os.fsync(parent_fd)
        finally:
            os.close(parent_fd)
    except OSError as exc:
        # Never unlink an uncertain one-shot ticket after a failed fsync.
        fail(f"ticket-durability-error:{exc.errno}")
    print(f"SAC_CONTROLLED_RETEST=ARMED_ONCE ticket={ticket}")


def main() -> None:
    if len(sys.argv) != 3 or sys.argv[1] not in ("inspect", "arm"):
        print("usage: authorize_daf_sac_controlled_retest.py inspect|arm"
              " <private-evidence-directory>", file=sys.stderr)
        raise SystemExit(2)
    folder = Path(sys.argv[2])
    try:
        record, digest, count = inspect(folder)
    except (OSError, UnicodeError, ValueError) as exc:
        fail(f"invalid-evidence:{type(exc).__name__}")
    if sys.argv[1] == "inspect":
        print("SAC_CONTROLLED_RETEST_PREFLIGHT=PASS")
        print(f"PREVIOUS_ARCHIVE={record}")
        print(f"PREVIOUS_ARCHIVE_SHA256={digest}")
        print(f"PREVIOUS_DTC_COUNT={count}")
        print("PREVIOUS_OUTCOME=UNKNOWN")
        return
    consume(folder, record, digest, count)


if __name__ == "__main__":
    main()
