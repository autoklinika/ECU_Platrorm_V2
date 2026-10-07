#!/usr/bin/env bash
set -euo pipefail

# Explicit operator-only physical gate. Read-only UDS commands at 250 kbit/s.
if [[ "${EUID}" -ne 0 ]]; then
  echo "ERROR: run with sudo; privilege is used only to set can0 state"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROBE="$ROOT_DIR/build/daf-sac-app-linux/tests/ecu_daf_sac_bench_app_probe"
IFACE="can0"
TARGET_USER="${SUDO_USER:-ecu}"

if [[ ! -x "$PROBE" ]]; then
  echo "ERROR: missing Stage 4 bench application probe: $PROBE"
  exit 1
fi

for required in ip runuser; do
  if ! command -v "$required" >/dev/null 2>&1; then
    echo "ERROR: missing required utility: $required"
    exit 1
  fi
done

if ! ip -details link show "$IFACE" >/dev/null 2>&1; then
  echo "ERROR: interface $IFACE does not exist"
  exit 1
fi

# Never interrupt another current use of the physical CAN interface.
if ip -details link show "$IFACE" | head -1 | grep -q 'state UP'; then
  echo "ERROR: $IFACE is already UP; do not interrupt another session"
  exit 1
fi

cleanup() {
  local exit_status=$?
  ip link set "$IFACE" down >/dev/null 2>&1 || true
  echo "STAGE4_CAN_LINK_AFTER=DOWN (operator may verify with ip link)"
  return "$exit_status"
}
trap cleanup EXIT

echo "[ECU V2] Stage 4 — DAF SAC first Bench Runtime application physical gate"
echo "Only UDS 0x22 F190/F188/F192; no state change, flash or actuator command."
echo "No direct hardware control from GUI, DUT Profile or Core V2."
echo "CAN: $IFACE / 250000 / Classic CAN / normal (ACK enabled)"
echo

ip link set "$IFACE" down
ip link set "$IFACE" type can bitrate 250000 fd off listen-only off
ip link set "$IFACE" up

echo "=== BEFORE ==="
ip -details -statistics link show "$IFACE"

echo "=== Bench-managed read-only session ==="
runuser -u "$TARGET_USER" -- "$PROBE" "$IFACE" 250000

echo "=== AFTER (before automatic DOWN) ==="
ip -details -statistics link show "$IFACE"
echo "DAF_SAC_STAGE4_BENCH_PHYSICAL_GATE=PASS"
