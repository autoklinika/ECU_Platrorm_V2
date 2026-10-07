#!/usr/bin/env bash
set -euo pipefail

# Deliberate operator-only DTC clear. Never part of CI / read-only probes.
# This script CAN transmit UDS 0x14 after an explicit interactive confirmation.
if [[ "${EUID}" -ne 0 ]]; then
  echo "ERROR: run with sudo from a terminal"
  exit 1
fi
if [[ "$#" -ne 0 ]]; then
  echo "Usage: sudo ./scripts/run_stage42_daf_sac_clear_gate.sh"
  exit 2
fi
if [[ ! -t 0 || ! -t 1 ]]; then
  echo "ERROR: interactive terminal required; piping and automation denied"
  exit 2
fi
if [[ -z "${SUDO_USER:-}" || "${SUDO_USER}" == root ]]; then
  echo "ERROR: run with sudo from a named non-root operator account"
  exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROBE="$ROOT_DIR/build/daf-sac-app-linux/tests/ecu_daf_sac_stage42_clear_probe"
IFACE="can0"
TARGET_USER="$SUDO_USER"

for binary in ip runuser getent cut; do
  if ! command -v "$binary" >/dev/null 2>&1; then
    echo "ERROR: missing command: $binary"
    exit 1
  fi
done
if [[ ! -x "$PROBE" ]]; then
  echo "ERROR: clear CLI not built: $PROBE"
  exit 1
fi

HOME_DIR="$(getent passwd "$TARGET_USER" | cut -d: -f6)"
if [[ -z "$HOME_DIR" || "$HOME_DIR" != /* ]]; then
  echo "ERROR: cannot determine operator's home directory"
  exit 1
fi
EVIDENCE_DIR="$HOME_DIR/.local/state/ecu-platform/daf-sac/dtc-clear"
runuser -u "$TARGET_USER" -- mkdir -p -- "$EVIDENCE_DIR"
runuser -u "$TARGET_USER" -- chmod 700 -- "$EVIDENCE_DIR"

if ! ip link show "$IFACE" >/dev/null 2>&1; then
  echo "ERROR: missing CAN interface $IFACE"
  exit 1
fi
if ip link show "$IFACE" | head -1 | grep -q 'state UP'; then
  echo "ERROR: $IFACE is already UP; refusing to disturb another session"
  exit 1
fi

cleanup() {
  local rc=$?
  ip link set "$IFACE" down >/dev/null 2>&1 || true
  echo "SAC_DTC_CLEAR_CAN_LINK_AFTER=DOWN"
  return "$rc"
}
trap cleanup EXIT

echo "[ECU V2] DAF SAC Stage 4.2 — OPERATOR-CONTROLLED DTC CLEAR"
echo "DANGER: this operation can erase recorded fault history."
echo "Before erase: 10 03 + 19 02 FF and a durable 0600 backup."
echo "On approval only: 10 03 + 14 FF FF FF; then DTC re-read."
echo "No automated acceptance; operator must type the displayed phrase."
echo "This DOES NOT change Core V2, Bench Runtime, power or outputs."
echo

ip link set "$IFACE" down
ip link set "$IFACE" type can bitrate 250000 fd off listen-only off
ip link set "$IFACE" up
echo "=== CAN BEFORE ==="
ip -details -statistics link show "$IFACE"
echo "=== DTC INSPECTION / INTERACTIVE CONFIRMATION ==="

runuser -u "$TARGET_USER" -- "$PROBE" "$IFACE" clear-dtc "$EVIDENCE_DIR"

echo "=== CAN AFTER (before cleanup) ==="
ip -details -statistics link show "$IFACE"
echo "SAC_DTC_CLEAR_OPERATOR_GATE=COMPLETE"
