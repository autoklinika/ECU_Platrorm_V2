#!/usr/bin/python3
"""Root-owned, restricted ECU Platform V2 CAN bench agent.

Public commands are ONLY status and root-installed DAF SAC read probes.
Never a sudo proxy, shell executor, ECU flash or DTC clear backend.
The read probe is dropped to the 'ecu' UID; privileged code controls can0.
"""
from __future__ import annotations

import argparse
import json
import logging
import os
from pathlib import Path
import pwd
import signal
import socket
import stat
import struct
import subprocess
import sys
import time

SOCKET_PATH = Path("/run/ecu-platform-v2-bench/request.sock")
PROBE_PATH = Path("/usr/local/libexec/ecu-platform-v2/sac-read-probe")
IP_PATH = "/usr/sbin/ip"
MODES = {"sac.read_dtc": "dtc", "sac.read_parameters": "parameters"}
MAX_REQUEST_BYTES = 512
PROBE_TIMEOUT_SECONDS = 14


def can_info() -> dict:
    p = subprocess.run(
        [IP_PATH, "-j", "-d", "link", "show", "dev", "can0"],
        check=True, text=True, capture_output=True, timeout=3,
    )
    values = json.loads(p.stdout)
    if len(values) != 1 or values[0].get("link_type") != "can":
        raise RuntimeError("can0 missing or unexpected device type")
    return values[0]


def run_ip(*args: str) -> None:
    subprocess.run(
        [IP_PATH, "link", "set", "can0", *args],
        check=True, capture_output=True, text=True, timeout=3,
    )


def down_can() -> bool:
    try:
        run_ip("down")
        return "UP" not in can_info().get("flags", [])
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError):
        logging.exception("Could not safely set can0 DOWN")
        return False


def valid_installed_probe() -> None:
    for path in (PROBE_PATH.parent, PROBE_PATH):
        info = path.lstat()
        if (info.st_uid != 0 or (info.st_mode & 0o022) or
                stat.S_ISLNK(info.st_mode)):
            raise RuntimeError("Read probe path is not root-owned and immutable")
    if not stat.S_ISREG(PROBE_PATH.stat().st_mode):
        raise RuntimeError("Missing immutable read-only probe")


def execute_read(command: str, operator: pwd.struct_passwd) -> dict:
    mode = MODES.get(command)
    if mode is None:
        return {"status": "denied", "message": "Operation not allowlisted"}
    started = time.monotonic()
    owns_can = False
    result = {"status": "failed", "message": "Read did not start"}
    try:
        valid_installed_probe()
        if "UP" in can_info().get("flags", []):
            return {"status": "busy", "message": "can0 already UP; no link changes"}
        # From this point all error paths attempt CAN DOWN.
        owns_can = True
        run_ip("down")
        run_ip("type", "can", "bitrate", "250000", "fd", "off",
               "listen-only", "off")
        run_ip("up")
        p = subprocess.run(
            [str(PROBE_PATH), "can0", mode],
            user=operator.pw_uid, group=operator.pw_gid, extra_groups=[],
            text=True, capture_output=True, check=False,
            timeout=PROBE_TIMEOUT_SECONDS, cwd="/",
            env={"PATH": "/usr/bin:/bin", "LANG": "C"},
        )
        output = (p.stdout or "") + (p.stderr or "")
        good = p.returncode == 0 and "SAC_STAGE42_READ_PHYSICAL=PASS" in output
        result = {
            "status": "pass" if good else "failed",
            "mode": mode,
            "exit_code": p.returncode,
            "output": output[:16000],
        }
    except subprocess.TimeoutExpired:
        result = {"status": "failed", "mode": mode, "message": "Probe timed out"}
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        result = {"status": "failed", "mode": mode, "message": str(exc)[:250]}
    finally:
        if owns_can:
            stopped = down_can()
            result["can0_cleanup"] = "DOWN" if stopped else "FAILED"
            if not stopped:
                result["status"] = "critical"
        result["elapsed_seconds"] = round(time.monotonic() - started, 3)
    return result


def dispatch(request: object, uid: int, operator: pwd.struct_passwd) -> dict:
    if uid != operator.pw_uid:
        return {"status": "denied", "message": "Invalid local user"}
    if not isinstance(request, dict) or set(request) != {"operation"}:
        return {"status": "denied", "message": "Only operation field accepted"}
    operation = request["operation"]
    if operation == "status":
        try:
            return {
                "status": "ready",
                "allowed": sorted(MODES),
                "can0_up": "UP" in can_info().get("flags", []),
            }
        except (OSError, RuntimeError, ValueError, subprocess.SubprocessError):
            return {"status": "unavailable"}
    if not isinstance(operation, str) or operation not in MODES:
        return {"status": "denied", "message": "Operation not allowed"}
    return execute_read(operation, operator)


def read_message(conn: socket.socket) -> object:
    conn.settimeout(2)
    raw = bytearray()
    while len(raw) <= MAX_REQUEST_BYTES:
        chunk = conn.recv(MAX_REQUEST_BYTES + 1 - len(raw))
        if not chunk:
            break
        raw.extend(chunk)
        if b"\n" in chunk:
            break
    if len(raw) > MAX_REQUEST_BYTES or not raw.endswith(b"\n"):
        raise ValueError("Invalid request length or framing")
    return json.loads(raw.decode("utf-8"))


def valid_runtime_directory(info: os.stat_result, group_gid: int,
                            effective_gid: int) -> bool:
    return (stat.S_ISDIR(info.st_mode) and info.st_uid == 0 and
            info.st_gid == group_gid and
            stat.S_IMODE(info.st_mode) == 0o750 and
            effective_gid == group_gid)


def valid_server_socket(info: os.stat_result, group_gid: int) -> bool:
    return (stat.S_ISSOCK(info.st_mode) and info.st_uid == 0 and
            info.st_gid == group_gid and
            stat.S_IMODE(info.st_mode) == 0o660)


def serve(operator_name: str) -> None:
    if os.geteuid() != 0:
        raise RuntimeError("Only root-owned systemd service may start agent")
    operator = pwd.getpwnam(operator_name)
    if operator.pw_uid == 0:
        raise RuntimeError("Cannot expose bench service to root identity")
    runtime_dir = SOCKET_PATH.parent
    info = runtime_dir.lstat()
    # systemd creates root:ecu, 0750 using the unit User/Group fields.
    # The agent only verifies ownership and never changes it.
    if not valid_runtime_directory(info, operator.pw_gid, os.getegid()):
        raise RuntimeError("Unsafe systemd RuntimeDirectory owner/group/mode")
    os.umask(0o077)
    # On systemd SIGTERM, unwind the active request through its finally
    # block (which attempts to return can0 DOWN before exiting).
    def terminate(signum: int, _frame: object) -> None:
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, terminate)
    signal.signal(signal.SIGINT, terminate)
    if SOCKET_PATH.exists() or SOCKET_PATH.is_symlink():
        old = SOCKET_PATH.lstat()
        if not stat.S_ISSOCK(old.st_mode) or old.st_uid != 0:
            raise RuntimeError("Unsafe existing server socket")
        SOCKET_PATH.unlink()

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
        server.bind(str(SOCKET_PATH))
        # Socket group comes from Group=ecu; only chmod is needed.
        os.chmod(SOCKET_PATH, 0o660)
        socket_info = SOCKET_PATH.lstat()
        if not valid_server_socket(socket_info, operator.pw_gid):
            raise RuntimeError("Unexpected socket ownership or permissions")
        server.listen(4)
        logging.info("ECU bench agent serving %s for uid %d",
                     SOCKET_PATH, operator.pw_uid)
        while True:
            with server.accept()[0] as conn:
                try:
                    creds = conn.getsockopt(socket.SOL_SOCKET,
                                            socket.SO_PEERCRED, 12)
                    _, uid, _ = struct.unpack("3i", creds)
                    request = read_message(conn)
                    result = dispatch(request, uid, operator)
                    logging.info(
                        "bench request uid=%s op=%s status=%s can_cleanup=%s",
                        uid, request.get("operation") if isinstance(request, dict) else None,
                        result.get("status"), result.get("can0_cleanup", "n/a"),
                    )
                except (OSError, RuntimeError, ValueError,
                        UnicodeError, TimeoutError) as exc:
                    result = {"status": "denied", "message": str(exc)[:180]}
                try:
                    conn.sendall((json.dumps(result) + "\n").encode("utf-8"))
                except (BrokenPipeError, ConnectionResetError, TimeoutError):
                    pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--operator", required=True)
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO)
    try:
        serve(args.operator)
    except (RuntimeError, OSError, ValueError) as exc:
        logging.error("Agent cannot start: %s", exc)
        sys.exit(1)
