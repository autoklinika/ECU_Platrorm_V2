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

for binary in ip runuser getent cut install grep; do
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
# Do not place DTC archives under ~/.local/state: on this host its
# existing ancestors are root-owned. Never change ownership of another
# application's data or grant world write access to those directories.
EVIDENCE_ROOT="$HOME_DIR/.ecu-platform-v2"
EVIDENCE_SAC="$EVIDENCE_ROOT/daf-sac"
EVIDENCE_DIR="$EVIDENCE_SAC/dtc-clear"
if [[ -L "$EVIDENCE_ROOT" || -L "$EVIDENCE_SAC" || -L "$EVIDENCE_DIR" ]]; then
  echo "ERROR: DTC evidence path contains a symbolic link"
  exit 1
fi
if ! runuser -u "$TARGET_USER" -- install -d -m 0700 -- \
    "$EVIDENCE_ROOT" "$EVIDENCE_SAC" "$EVIDENCE_DIR"; then
  echo "ERROR: cannot create private DTC evidence directory as $TARGET_USER"
  exit 1
fi
if ! runuser -u "$TARGET_USER" -- test -w "$EVIDENCE_DIR"; then
  echo "ERROR: DTC evidence directory is not writable by $TARGET_USER"
  exit 1
fi

# Never offer a second erase while a previous destructive request has
# an unresolved result. This check is BEFORE the CAN interface is enabled.
for record in "$EVIDENCE_DIR"/sac-dtc-*.txt; do
  [[ -f "$record" ]] || continue
  if grep -q '^CLEAR_OUTCOME=UNKNOWN' "$record"; then
    echo "DTC_CLEAR_BLOCKED=PREVIOUS_OUTCOME_UNKNOWN"
    echo "Previous uncertain operation: $record"
    echo "Perform a fresh READ-ONLY DTC check and review the result first."
    exit 4
  fi
done

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
