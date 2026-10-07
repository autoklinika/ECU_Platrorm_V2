#!/usr/bin/env bash
set -euo pipefail

# Explicitly operator-triggered hardware gate; never auto-run in CI.
# Read-only services EXCEPT the optional 10 03 extended-session request
# required by the legacy SAC DTC-read sequence.
if [[ "${EUID}" -ne 0 ]]; then
  echo "ERROR: run as sudo for CAN link configuration"
  exit 1
fi
if [[ "$#" -ne 1 || ( "$1" != "parameters" && "$1" != "dtc" ) ]]; then
  echo "Usage: sudo ./scripts/run_stage42_daf_sac_read_gate.sh parameters|dtc"
  echo "DTC erase is intentionally not available in this physical runner."
  exit 2
fi

MODE="$1"
IFACE="can0"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROBE="$ROOT/build/daf-sac-app-linux/tests/ecu_daf_sac_stage42_read_probe"
TARGET_USER="${SUDO_USER:-ecu}"

if [[ ! -x "$PROBE" ]]; then
  echo "ERROR: missing physical read-only probe: $PROBE"
  exit 1
fi
if ! command -v ip >/dev/null 2>&1 || ! command -v runuser >/dev/null 2>&1; then
  echo "ERROR: ip and runuser required"
  exit 1
fi
if ! ip link show "$IFACE" >/dev/null 2>&1; then
  echo "ERROR: interface $IFACE not available"
  exit 1
fi
if ip -details link show "$IFACE" | head -1 | grep -q 'state UP'; then
  echo "ERROR: CAN already UP, refusing to disturb another owner"
  exit 1
fi

cleanup() {
  local status=$?
  ip link set "$IFACE" down >/dev/null 2>&1 || true
  echo "SAC_STAGE42_LINK_CLEANUP=DOWN"
  return "$status"
}
trap cleanup EXIT

echo "[ECU V2] DAF SAC Stage 4.2 read-only physical proof"
echo "Mode: $MODE"
echo "CAN 250000 Classic normal; no write, no ClearDiagnosticInformation, no flash."
if [[ "$MODE" == "dtc" ]]; then
  echo "DTC read requires UDS extended session 10 03 then 19 02 FF."
else
  echo "Voltage: 22 FE96; pressure from passive J1939 PGN 65198."
fi

ip link set "$IFACE" down
ip link set "$IFACE" type can bitrate 250000 fd off listen-only off
ip link set "$IFACE" up

echo "=== CAN BEFORE ==="
ip -details -statistics link show "$IFACE"
echo "=== SAC READ ==="
runuser -u "$TARGET_USER" -- "$PROBE" "$IFACE" "$MODE"
echo "=== CAN AFTER (before DOWN) ==="
ip -details -statistics link show "$IFACE"
echo "SAC_STAGE42_READ_GATE=PASS mode=$MODE"
