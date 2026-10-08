#!/usr/bin/env python3
"""Unprivileged permanent client for authorized ECU Platform V2 bench agent.

No sudo, no password, no privileged shell. Agent must first be installed
once by the owner using scripts/install_ecu_bench_agent.sh.
"""
from __future__ import annotations

import argparse
import json
import socket
import sys

SOCKET = "/run/ecu-platform-v2-bench/request.sock"
OPS = {
    "status": "status",
    "sac-dtc": "sac.read_dtc",
    "sac-parameters": "sac.read_parameters",
}


def request(operation: str) -> dict:
    command = OPS[operation]
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as conn:
        conn.settimeout(40)
        conn.connect(SOCKET)
        conn.sendall((json.dumps({"operation": command}) + "\n").encode())
        response = bytearray()
        while len(response) < 65536:
            chunk = conn.recv(65536 - len(response))
            if not chunk:
                break
            response.extend(chunk)
            if b"\n" in chunk:
                break
    if not response.endswith(b"\n"):
        raise RuntimeError("Invalid or oversized agent response")
    value = json.loads(response)
    if not isinstance(value, dict) or "status" not in value:
        raise RuntimeError("Invalid agent contract")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=OPS)
    args = parser.parse_args()
    try:
        result = request(args.operation)
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"ECU_BENCH_AGENT=UNAVAILABLE reason={exc}", file=sys.stderr)
        print("One-time installation by the operator is required.")
        return 2

    print(f"ECU_BENCH_AGENT_STATUS={result['status'].upper()}")
    if "allowed" in result:
        print("AVAILABLE_OPERATIONS=" + ",".join(result["allowed"]))
    if "can0_up" in result:
        print(f"CAN0_UP={int(result['can0_up'])}")
    if "mode" in result:
        print(f"SAC_READ_MODE={result['mode']}")
    if "output" in result:
        print(result["output"], end="" if result["output"].endswith("\n") else "\n")
    if "exit_code" in result:
        print(f"PROBE_EXIT_CODE={result['exit_code']}")
    if "can0_cleanup" in result:
        print(f"CAN0_CLEANUP={result['can0_cleanup']}")
    if "message" in result:
        print(f"DETAIL={result['message']}")
    return 0 if result["status"] in ("pass", "ready") else 1


if __name__ == "__main__":
    sys.exit(main())
