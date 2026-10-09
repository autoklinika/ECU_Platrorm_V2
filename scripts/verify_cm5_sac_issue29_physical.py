#!/usr/bin/env python3
"""Exactly two operator-authorized read-only DAF SAC captures through kiosk.
Never run without --physical. No direct socket CAN, raw CAN, or credentials.
"""
import argparse
import hashlib
import http.client
import json
from pathlib import Path
import subprocess
import sys

HOST = "127.0.0.1"
PORT = 8877
PARAMS = "/kiosk/v1/readouts/daf-sac/parameters/latest"
CONNECT = "/kiosk/v1/bench/daf-sac/connect"
DTC = Path("/var/lib/ecu-platform-v2/api-readouts/dtc-latest.v1")
PROFILES = {250000: 0xDAF00025, 500000: 0xDAF00050}


def request(method, route):
    conn = http.client.HTTPConnection(HOST, PORT, timeout=65 if method == "POST" else 5)
    try:
        conn.request(method, route, body=b"" if method == "POST" else None,
                     headers={"Host": f"{HOST}:{PORT}", "X-ECU-Kiosk": "v1",
                              "Origin": f"http://{HOST}:{PORT}",
                              "Accept": "application/json"})
        response = conn.getresponse()
        raw = response.read(32769)
        if len(raw) > 32768:
            raise RuntimeError("Oversized kiosk response")
        value = json.loads(raw)
        if (response.status != 200 or value.get("schema_version") != 1
                or not isinstance(value.get("data"), dict)):
            error = value.get("error", {}).get("code", "invalid_backend_response")
            raise RuntimeError(f"{route}: status={response.status} code={error}")
        return value["data"]
    finally:
        conn.close()


def can_down():
    # Pure query, not an interface operation.
    result = subprocess.run(["/usr/sbin/ip", "-j", "link", "show", "dev", "can0"],
                            capture_output=True, check=True, text=True, timeout=4)
    links = json.loads(result.stdout)
    if len(links) != 1 or "UP" in links[0].get("flags", []):
        raise RuntimeError("can0 is UP or unavailable")


def check_capture(identity, capture):
    if (identity.get("parameters_published") is not True or
            identity.get("parameters_status") != "completed" or
            PROFILES.get(identity.get("bitrate")) != identity.get("profile_id")):
        raise RuntimeError("No verified completed parameter readout")
    if (capture.get("source") != "completed_application_operation" or
            capture.get("live") is not False or
            capture.get("profile_id") != identity["profile_id"] or
            capture.get("captured_at_unix_ms") !=
                identity.get("parameter_captured_at_unix_ms") or
            capture.get("completed_generation") !=
                identity.get("parameter_completed_generation") or
            not isinstance(capture.get("parameters"), dict)):
        raise RuntimeError("Capture provenance mismatch")
    p = capture["parameters"]
    for key in ("permanent_voltage_v", "ignition_voltage_v"):
        v = p.get(key)
        if isinstance(v, bool) or not isinstance(v, (int, float)) or not 0 <= v <= 60:
            raise RuntimeError(f"Invalid FE96 {key}")
    for key in ("pressure1_bar", "pressure2_bar"):
        v = p.get(key)
        if v is not None and (isinstance(v, bool) or
                              not isinstance(v, (int, float)) or not 0 <= v <= 20.24):
            raise RuntimeError(f"Invalid FEAE {key}")
    if type(p.get("pgn_feae_observed")) is not bool:
        raise RuntimeError("Invalid FEAE observed flag")
    return p


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--physical", action="store_true", required=True,
                        help="operator-authorized read-only two-cycle measurement")
    opts = parser.parse_args()
    if not opts.physical:
        return 77
    before = hashlib.sha256(DTC.read_bytes()).hexdigest()
    captures = []
    sw_hw = None
    seen_feae = False
    for cycle in (1, 2):
        can_down()
        identity = request("POST", CONNECT)
        capture = request("GET", PARAMS)
        p = check_capture(identity, capture)
        can_down()
        if hashlib.sha256(DTC.read_bytes()).hexdigest() != before:
            raise RuntimeError("DTC evidence modified")
        fingerprint = (identity.get("software"), identity.get("hardware"),
                       identity.get("profile_id"), identity.get("bitrate"))
        if sw_hw is not None and fingerprint != sw_hw:
            raise RuntimeError("DUT changed between measurements")
        sw_hw = fingerprint
        if captures and capture["captured_at_unix_ms"] <= captures[-1]:
            raise RuntimeError("Old capture was reused as new")
        captures.append(capture["captured_at_unix_ms"])
        seen_feae = seen_feae or p["pgn_feae_observed"]
        pressure = lambda key: ("UNAVAILABLE" if p[key] is None else f"{p[key]:.2f}bar")
        print(f"SAC_ISSUE29_PHYSICAL_CYCLE_{cycle}=PASS bitrate={identity['bitrate']} "
              f"FE96={p['permanent_voltage_v']:.1f}/{p['ignition_voltage_v']:.1f}V "
              f"FEAE={p['pgn_feae_observed']} "
              f"pressure1={pressure('pressure1_bar')} pressure2={pressure('pressure2_bar')}",
              flush=True)
    if not seen_feae:
        raise RuntimeError("No physical FEAE frame observed across two cycles")
    can_down()
    print("SAC_ISSUE29_PHYSICAL=PASS independent-new-captures=2 "
          "can0=DOWN dtc=UNCHANGED FEAE=OBSERVED", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, TimeoutError,
            subprocess.SubprocessError, json.JSONDecodeError) as exc:
        print(f"SAC_ISSUE29_PHYSICAL=FAIL {exc}", file=sys.stderr)
        sys.exit(2)
