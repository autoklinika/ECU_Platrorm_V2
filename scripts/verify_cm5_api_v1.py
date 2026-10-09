#!/usr/bin/env python3
"""CM5 API V1 post-install smoke: no CAN TX, no Bench Agent operations."""
from __future__ import annotations

import argparse
import http.client as http_client
import json
import os
import grp
import pwd
from pathlib import Path
import socket
import subprocess
import sys


def check(ok: bool, tag: str) -> None:
    if not ok:
        raise RuntimeError("ECU_API_DEPLOY_" + tag + "=FAIL")
    print("ECU_API_DEPLOY_" + tag + "=PASS")


def run(*args: str) -> str:
    return subprocess.check_output(
        args, text=True, stderr=subprocess.DEVNULL, timeout=5
    ).strip()


def api_request(method: str, route: str, auth: str | None,
         extra: dict[str, str] | None = None) -> tuple[int, dict, dict]:
    conn = http_client.HTTPConnection("127.0.0.1", 8878, timeout=3)
    try:
        headers = dict(extra or {})
        if auth is not None:
            headers["Authorization"] = "Bearer " + auth
        conn.request(method, route, headers=headers)
        resp = conn.getresponse()
        payload = resp.read(16384)
        data = json.loads(payload) if payload else {}
        return resp.status, dict(resp.getheaders()), data
    finally:
        conn.close()


def reject_agent_socket(user: str) -> bool:
    source = (
        "import socket,sys;"
        "s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);"
        "p='/run/ecu-platform-v2-bench/request.sock';"
        "\ntry:\n s.connect(p)\n"
        "except PermissionError:\n sys.exit(0)\n"
        "except OSError:\n sys.exit(1)\n"
        "else:\n sys.exit(2)\n"
    )
    return subprocess.run(
        ["runuser", "-u", user, "--", "/usr/bin/python3", "-I", "-c", source],
        check=False, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, timeout=5
    ).returncode == 0


def reject_token_read_as_kiosk() -> bool:
    source = (
        "import pathlib,sys\n"
        "try:\n pathlib.Path('/etc/ecu-platform-v2/api/token').read_bytes()\n"
        "except PermissionError:\n sys.exit(0)\n"
        "else:\n sys.exit(2)\n"
    )
    return subprocess.run(
        ["runuser", "-u", "ecu-kiosk", "--", "/usr/bin/python3", "-I",
         "-c", source],
        check=False, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, timeout=5
    ).returncode == 0


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected-revision", required=True)
    args = parser.parse_args()
    if os.geteuid() != 0:
        raise RuntimeError("root is required for protected-token smoke")
    token_path = Path("/etc/ecu-platform-v2/api/token")
    token_stat = token_path.stat()
    token_gid = grp.getgrnam("ecu-api").gr_gid
    read_gid = grp.getgrnam("ecu-api-read").gr_gid
    producer_uid = pwd.getpwnam("ecu").pw_uid
    data_path = Path("/var/lib/ecu-platform-v2/api-readouts")
    data_stat = data_path.stat()
    check(token_stat.st_mode & 0o7777 == 0o640 and
          token_stat.st_uid == 0 and token_stat.st_gid == token_gid and
          not token_path.is_symlink(), "TOKEN_MODE")
    check(data_stat.st_mode & 0o7777 == 0o2750 and
          data_stat.st_uid == producer_uid and
          data_stat.st_gid == read_gid and not data_path.is_symlink(),
          "SNAPSHOT_DIRECTORY_PERMISSIONS")
    token = token_path.read_text(encoding="ascii").strip()
    check(len(token) == 64 and all(c in "0123456789abcdef" for c in token),
          "TOKEN_FORMAT")

    for service in ("ecu-kiosk.service", "ecu-webgui-static.service",
                    "ecu-platform-v2-bench-agent.service",
                    "ecu-api-v1.service"):
        check(run("systemctl", "is-active", service) == "active",
              "SERVICE_" + service.split(".")[0].replace("-", "_").upper())
    check(run("systemctl", "show", "ecu-api-v1.service",
              "-p", "User", "--value") == "ecu-api", "SERVICE_IDENTITY")
    check(run("systemctl", "show", "ecu-api-v1.service",
              "-p", "Group", "--value") == "ecu-api", "SERVICE_GROUP")
    groups = set(run("id", "-nG", "ecu-api").split())
    check(groups == {"ecu-api", "ecu-api-read"}, "API_GROUP_ALLOWLIST")
    check(reject_agent_socket("ecu-api"), "API_AGENT_SOCKET_DENIED")
    check(reject_agent_socket("ecu-kiosk"), "KIOSK_AGENT_SOCKET_DENIED")
    check(reject_token_read_as_kiosk(), "KIOSK_TOKEN_DENIED")

    lines = run("ss", "-H", "-lnt").splitlines()
    listeners = [line.split()[3] for line in lines if line.split()[3].endswith(":8878")]
    check(listeners == ["127.0.0.1:8878"], "LISTEN_LOOPBACK_ONLY")

    code, headers, about = api_request("GET", "/api/v1/about", token)
    check(code == 200 and
          about.get("data", {}).get("api_version") == "v1" and
          about["data"].get("read_only") is True and
          about["data"].get("build_revision") == args.expected_revision,
          "API_BUILD_AND_READONLY")
    check(headers.get("Cache-Control") == "no-store", "CACHE_DISABLED")
    code, _, _ = api_request("GET", "/api/v1/about", None)
    check(code == 401, "BEARER_REQUIRED")
    code, _, _ = api_request("GET", "/api/v1/about", "f" * 64)
    check(code == 401, "BEARER_INVALID_DENIED")
    code, _, _ = api_request("GET", "/api/v1/about", token,
                       {"Origin": "https://untrusted.invalid"})
    check(code == 403, "ORIGIN_DENIED")
    code, _, _ = api_request("POST", "/api/v1/about", token)
    check(code in (400, 405), "WRITE_METHOD_DENIED")
    code, _, _ = api_request("GET", "/api/v1/can/transmit", token)
    check(code == 404, "CAN_TX_ROUTE_ABSENT")

    code, _, link = api_request("GET", "/api/v1/interfaces", token)
    actual = json.loads(run("ip", "-j", "-d", "link", "show", "can0"))[0]
    check(code == 200 and len(link["data"]["interfaces"]) == 1 and
          link["data"]["interfaces"][0]["name"] == "can0" and
          link["data"]["interfaces"][0]["up"] == ("UP" in actual["flags"]),
          "ACTUAL_CAN_NETLINK")
    check("UP" not in actual["flags"], "CAN_REMAINS_DOWN")
    code, _, errors = api_request("GET", "/api/v1/dut/dtcs", token)
    check(code == 503 and errors["error"]["code"] == "backend_unavailable",
          "LIVE_DTC_FAIL_CLOSED")
    code, _, errors = api_request("GET", "/api/v1/readouts/dtc/latest", token)
    check(code == 503 and errors["error"]["code"] == "backend_unavailable",
          "MISSING_READOUT_FAIL_CLOSED")
    web = http_client.HTTPConnection("127.0.0.1", 8877, timeout=3)
    try:
        web.request("GET", "/")
        response = web.getresponse()
        check(response.status == 200, "WEBGUI_UNCHANGED")
        response.read()
    finally:
        web.close()
    print("ECU_API_CM5_INSTALL_SMOKE=PASS")


if __name__ == "__main__":
    try:
        main()
    except (OSError, KeyError, ValueError, RuntimeError,
            subprocess.SubprocessError, json.JSONDecodeError) as exc:
        print("ECU_API_CM5_INSTALL_SMOKE=FAIL " + str(exc), file=sys.stderr)
        raise SystemExit(1) from None
