#!/usr/bin/python3
"""Linux-only, fixed-profile DAF SAC read-only connection adapter.

This isolated application adapter owns can0 for ONE operator-initiated
request. The browser never gets device, agent socket or shell access.
"""
from __future__ import annotations

import hmac
import json
import os
from pathlib import Path
import pwd
import re
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HOST, PORT = "127.0.0.1", 8879
SAC_CONNECT_PATH = "/api/v1/bench/daf-sac/connect"
ORIGIN = "http://127.0.0.1:8877"
TOKEN_FILE = Path("/etc/ecu-platform-v2/api/token")
PROBE_IDENT = Path("/usr/local/libexec/ecu-platform-v2/sac-identity-500k-probe")
PROBE_PARAMS = Path("/usr/local/libexec/ecu-platform-v2/sac-parameters-500k-probe")
READOUT_DIR = "/var/lib/ecu-platform-v2/api-readouts"
IP = "/usr/sbin/ip"
PROFILE = 0xDAF00050
BUSY = threading.Lock()
MAX_OUTPUT = 16384
IDENTIFICATION_START = (
    "START tx=0x18da30f9 rx=0x18daf930 "
    "bitrate=500000 mode=read-only-identification"
)


class Refused(Exception):
    def __init__(self, reason: str, http_status: int = 503):
        super().__init__(reason)
        self.reason = reason
        self.http_status = http_status


def ip(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([IP, *args], check=True, capture_output=True,
                          text=True, timeout=3)


def is_up() -> bool:
    response = json.loads(ip("-j", "-d", "link", "show", "dev", "can0").stdout)
    if len(response) != 1 or response[0].get("link_type") != "can":
        raise Refused("interface_unavailable")
    return "UP" in response[0].get("flags", [])


def executable_ready(path: Path) -> bool:
    # Source tree and writable binaries must never be executed by root-owned service.
    info = path.lstat()
    parent = path.parent.lstat()
    return (info.st_uid == 0 and parent.st_uid == 0 and
            not (info.st_mode & 0o022) and
            not (parent.st_mode & 0o022) and
            path.is_file() and not path.is_symlink() and os.access(path, os.X_OK))


def run_probe(path: Path, args: list[str], operator: pwd.struct_passwd) -> str:
    if not executable_ready(path):
        raise Refused("probe_unavailable")
    try:
        job = subprocess.run([str(path), *args], user=operator.pw_uid,
                             group=operator.pw_gid, extra_groups=[],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             text=True, timeout=17, check=False, cwd="/",
                             env={"PATH": "/usr/bin:/bin", "LANG": "C"})
    except subprocess.TimeoutExpired as exc:
        raise Refused("communication_timeout") from exc
    if job.returncode != 0 or len(job.stdout) > MAX_OUTPUT:
        raise Refused("communication_failed")
    return job.stdout


def fields(stdout: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in stdout.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key == "SAC_PHYSICAL_PROBE" and value.startswith("START"):
            # The native probe deliberately emits START and then PASS under
            # the same key. They are different stages, not duplicate replies.
            if "SAC_PROBE_START" in result:
                raise Refused("invalid_identity")
            result["SAC_PROBE_START"] = value
            continue
        if key in result:
            # Do not allow repeated result, VIN, SW or HW markers.
            if key.startswith(("SAC_VIN", "SAC_SOFTWARE",
                               "SAC_HARDWARE", "SAC_PHYSICAL_PROBE")):
                raise Refused("invalid_identity")
        result[key] = value
    return result


def decode_identity(stdout: str) -> dict:
    data = fields(stdout)
    if data.get("SAC_PROBE_START") != IDENTIFICATION_START:
        raise Refused("invalid_identity")
    if data.get("SAC_PHYSICAL_PROBE") != "PASS CORE_V2_UDS_ISOTP_CAN":
        raise Refused("communication_failed")
    vin_status = data.get("SAC_VIN_STATUS")
    if vin_status == "UNPROGRAMMED_FF17":
        if data.get("SAC_IDENTIFICATION_COMPLETENESS") != "PARTIAL_NO_VIN":
            raise Refused("invalid_identity")
        vin = None
    elif vin_status == "VALID_ASCII":
        vin = data.get("SAC_VIN", "")
        if re.fullmatch(r"[A-Za-z0-9]{17}", vin) is None:
            raise Refused("invalid_identity")
    else:
        raise Refused("invalid_identity")
    software = data.get("SAC_SOFTWARE", "").strip()
    hardware = data.get("SAC_HARDWARE", "").strip()
    for value in (software, hardware):
        if not value or len(value) > 64 or re.fullmatch(r"[ -~]+", value) is None:
            raise Refused("invalid_identity")
    return {"vin": vin, "vin_status": vin_status, "software": software,
            "hardware": hardware, "profile_id": PROFILE}


def connect_once(operator: pwd.struct_passwd) -> dict:
    if is_up():
        raise Refused("bench_busy", 409)
    # Only this adapter sets CAN link state. It never flashes, clears, resets,
    # changes diagnostic session or controls outputs.
    owns_can = True
    result = None
    fault = None
    capture_floor = int(time.time() * 1000)
    try:
        ip("link", "set", "can0", "down")
        ip("link", "set", "can0", "type", "can", "bitrate", "500000",
           "fd", "off", "listen-only", "off")
        ip("link", "set", "can0", "up")
        state = json.loads(ip("-j", "-d", "link", "show", "dev", "can0").stdout)[0]
        can = state.get("linkinfo", {}).get("info_data", {})
        if (not "UP" in state.get("flags", []) or
                can.get("bittiming", {}).get("bitrate") != 500000 or
                can.get("state") == "BUS-OFF" or can.get("listen-only", False)):
            raise Refused("interface_unavailable")
        identity = decode_identity(
            run_probe(PROBE_IDENT, ["can0", "500000"], operator))
        published = False
        try:
            param_out = run_probe(
                PROBE_PARAMS, ["can0", "parameters", "500000", READOUT_DIR], operator)
            report = fields(param_out)
            published = (report.get("SAC_STAGE42_READ_PHYSICAL") == "PASS" and
                         report.get("SAC_API_PARAMETERS_READOUT_PUBLISHED", "").startswith("PASS"))
        except Refused:
            # An identified ECU may lack voltage/pressure DID support.
            # This is NOT grounds to fabricate a parameter readout.
            published = False
        result = {**identity, "parameters_published": published,
                  "parameter_capture_floor_ms": capture_floor}
    except Refused as exc:
        fault = exc
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        fault = Refused("interface_unavailable")
    finally:
        if owns_can:
            try:
                ip("link", "set", "can0", "down")
                if is_up():
                    raise RuntimeError("CAN remained up")
            except (OSError, ValueError, RuntimeError, subprocess.SubprocessError):
                fault = Refused("can_cleanup_failed")
    if fault:
        raise fault
    assert result is not None
    return result


class Handler(BaseHTTPRequestHandler):
    server_version = "ECUSacConnect/1"
    protocol_version = "HTTP/1.0"

    def log_message(self, *_args: object) -> None:
        # Never log bearer credentials or ECU identity.
        return

    def send_json(self, status: int, content: dict, allow_origin: bool) -> None:
        body = json.dumps({"schema_version": 1, **content}, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        if allow_origin:
            self.send_header("Access-Control-Allow-Origin", ORIGIN)
            self.send_header("Vary", "Origin")
        self.end_headers()
        self.wfile.write(body)

    def protected(self) -> bool:
        origin = self.headers.get("Origin", "")
        return (self.path == "/api/v1/bench/daf-sac/connect" and
                self.headers.get("Host") == f"{HOST}:{PORT}" and
                (not origin or origin == ORIGIN))

    def do_OPTIONS(self) -> None:
        if (not self.protected() or self.headers.get("Origin") != ORIGIN or
                self.headers.get("Access-Control-Request-Method") != "POST" or
                self.headers.get("Access-Control-Request-Headers", "").lower() != "authorization"):
            self.send_json(403, {"error": {"code": "origin_forbidden"}}, False)
            return
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", ORIGIN)
        self.send_header("Access-Control-Allow-Methods", "POST")
        self.send_header("Access-Control-Allow-Headers", "Authorization")
        self.send_header("Access-Control-Max-Age", "0")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_POST(self) -> None:
        allowed_origin = self.headers.get("Origin", "") == ORIGIN
        if not self.protected():
            self.send_json(403, {"error": {"code": "origin_forbidden"}}, False)
            return
        if self.headers.get("Content-Length") not in (None, "0") or self.headers.get("Transfer-Encoding"):
            self.send_json(400, {"error": {"code": "invalid_request"}}, allowed_origin)
            return
        auth = self.headers.get("Authorization", "")
        if not hmac.compare_digest(auth, "Bearer " + self.server.token):
            self.send_json(401, {"error": {"code": "unauthorized"}}, allowed_origin)
            return
        if not BUSY.acquire(blocking=False):
            self.send_json(409, {"error": {"code": "bench_busy"}}, allowed_origin)
            return
        try:
            try:
                answer = connect_once(self.server.operator)
                self.send_json(200, {"data": answer}, allowed_origin)
            except Refused as exc:
                self.send_json(exc.http_status, {"error": {"code": exc.reason}}, allowed_origin)
        finally:
            BUSY.release()

    def do_GET(self) -> None:
        self.send_json(405, {"error": {"code": "method_not_allowed"}}, False)


def serve() -> None:
    if os.geteuid() != 0:
        raise RuntimeError("Root-owned systemd adapter required")
    token = TOKEN_FILE.read_text(encoding="ascii").strip()
    if not re.fullmatch(r"[0-9a-f]{64}", token):
        raise RuntimeError("Invalid API token")
    operator = pwd.getpwnam("ecu")
    if operator.pw_uid == 0 or any(not executable_ready(p) for p in (PROBE_IDENT, PROBE_PARAMS)):
        raise RuntimeError("Unsafe operator or installed immutable probes")
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    server.daemon_threads = True
    server.token = token
    server.operator = operator
    server.serve_forever(poll_interval=0.5)


if __name__ == "__main__":
    serve()
