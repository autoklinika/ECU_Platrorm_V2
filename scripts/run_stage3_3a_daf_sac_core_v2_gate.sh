#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "ERROR: run with sudo"
  exit 1
fi

if [[ "${#}" -ne 1 ]]; then
  echo "usage: sudo $0 <250000|500000>"
  exit 2
fi

BITRATE="$1"
case "$BITRATE" in
  250000|500000)
    ;;
  *)
    echo "ERROR: DAF SAC proof accepts only evidence-backed 250000 or 500000 bit/s"
    exit 2
    ;;
esac

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROBE="$ROOT_DIR/build/daf-sac-core-v2-probe/tests/ecu_daf_sac_core_v2_probe"
TARGET_USER="${SUDO_USER:-ecu}"
IFACE="can0"

if [[ ! -x "$PROBE" ]]; then
  echo "ERROR: missing Core V2 DAF SAC probe binary: $PROBE"
  echo "Build target ecu_daf_sac_core_v2_probe first."
  exit 1
fi

cleanup() {
  ip link set "$IFACE" down >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "[ECU V2] Stage 3.3A — DAF SAC physical Core V2 proof"
echo "Interface: $IFACE"
echo "Bitrate: $BITRATE"
echo "Addressing: tester->SAC 0x18DA30F9, SAC->tester 0x18DAF930"
echo "Read-only sequence: UDS 0x22 F190 (VIN), F188 (software), F192 (hardware)"
echo "No session change, reset, write, security, routine, output-control or flash command is used."

ip link set "$IFACE" down
ip link set "$IFACE" type can   bitrate "$BITRATE"   fd off   listen-only off
ip link set "$IFACE" up

echo
echo "=== CAN state before Core V2 read-only probe ==="
ip -details -statistics link show "$IFACE"

echo
echo "=== Core V2 DAF SAC read-only proof as user $TARGET_USER ==="
runuser -u "$TARGET_USER" --   "$PROBE" "$IFACE" "$BITRATE"

echo
echo "=== CAN state after Core V2 read-only probe ==="
ip -details -statistics link show "$IFACE"

echo
echo "DAF_SAC_STAGE3_3A_PHYSICAL_GATE=PASS"
echo "The interface will now be returned to DOWN by the cleanup trap."
