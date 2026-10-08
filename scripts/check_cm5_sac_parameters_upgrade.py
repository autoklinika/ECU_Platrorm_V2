#!/usr/bin/env python3
"""Root-only, secret-safe live API smoke after scoped parameters binary upgrade."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import stat
import subprocess
import sys

# -I does not prepend the script directory. Import only our fixed helper.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_cm5_api_v1 import api_request

def check(valid: bool, marker: str) -> None:
    if not valid:
        raise RuntimeError("SAC_API_UPGRADE_" + marker + "=FAIL")
    print("SAC_API_UPGRADE_" + marker + "=PASS")

def main() -> None:
    args = argparse.ArgumentParser()
    args.add_argument("--expected-revision", required=True)
    parsed = args.parse_args()
    check(os.geteuid() == 0, "ROOT")
    key_file = Path("/etc/ecu-platform-v2/api/token")
    key_stat = key_file.lstat()
    check(stat.S_ISREG(key_stat.st_mode) and
          stat.S_IMODE(key_stat.st_mode) == 0o640 and
          key_stat.st_uid == 0, "PROTECTED_TOKEN")
    token = key_file.read_text(encoding="ascii").strip()
    check(len(token) == 64 and
          all(c in "0123456789abcdef" for c in token), "TOKEN_FORMAT")
    status, _, payload = api_request("GET", "/api/v1/about", token)
    check(status == 200 and payload["data"]["read_only"] is True and
          payload["data"]["build_revision"] == parsed.expected_revision,
          "EXACT_BUILD")
    status, _, _ = api_request("GET", "/api/v1/about", None)
    check(status == 401, "BEARER_REQUIRED")
    status, _, _ = api_request(
        "GET", "/api/v1/readouts/daf-sac/parameters/latest", None)
    check(status == 401, "PARAMETER_BEARER_REQUIRED")
    status, _, payload = api_request(
        "GET", "/api/v1/readouts/daf-sac/parameters/latest", token)
    check(status in (200, 410, 503) and payload["schema_version"] == 1,
          "PARAMETER_ENDPOINT_FAIL_CLOSED")
    if status == 200:
        value = payload["data"]
        check(value["source"] == "completed_application_operation" and
              value["live"] is False and
              value["profile_id"] in (0xDAF00025, 0xDAF00050),
              "PARAMETER_HISTORICAL_ONLY")
    status, _, payload = api_request(
        "GET", "/api/v1/readouts/dtc/latest", token)
    # An old preserved physical readout may legitimately expire before
    # an upgrade. Expiry (410) is not loss of evidence or reason for another
    # ECU diagnostic request.
    preserved_dtc = (
        (status == 200 and
         payload["data"]["source"] == "completed_application_operation" and
         payload["data"]["live"] is False and
         isinstance(payload["data"]["dtcs"]["entries"], list))
        or (status == 410 and
            payload["error"]["code"] == "readout_expired"))
    check(preserved_dtc, "PRESERVED_REAL_DTC_READOUT")
    status, _, _ = api_request(
        "POST", "/api/v1/readouts/daf-sac/parameters/latest", token)
    check(status == 405, "WRITE_METHOD_DENIED")
    status, _, _ = api_request(
        "GET", "/api/v1/can/transmit", token)
    check(status == 404, "CAN_TX_ROUTE_ABSENT")
    status, headers, _ = api_request(
        "OPTIONS", "/api/v1/readouts/daf-sac/parameters/latest", None,
        {"Origin": "http://127.0.0.1:8877",
         "Access-Control-Request-Method": "GET",
         "Access-Control-Request-Headers": "authorization"})
    check(status == 204 and
          headers.get("Access-Control-Allow-Origin") ==
          "http://127.0.0.1:8877", "CORS_KIOSK_ORIGIN")
    status, _, _ = api_request(
        "GET", "/api/v1/about", token,
        {"Origin": "http://localhost:8877"})
    check(status == 403, "HOST_ORIGIN_ISOLATION")
    can = subprocess.run(["ip", "-details", "link", "show", "can0"],
                         capture_output=True, text=True, check=True)
    check("state DOWN" in can.stdout, "CAN_UNCHANGED_DOWN")
    print("SAC_API_UPGRADE_SMOKE=PASS")

if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"SAC_API_UPGRADE_SMOKE=FAIL type={type(error).__name__}",
              file=sys.stderr)
        raise SystemExit(1)
