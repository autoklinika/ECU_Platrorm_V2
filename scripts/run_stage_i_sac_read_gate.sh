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
    echo "ERROR: Stage I SAC gate accepts only evidence-backed 250000 or 500000 bit/s"
    exit 2
    ;;
esac

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROBE="$ROOT_DIR/build/stage-i-debug/tests/ecu_sac_uds_read_probe"
TARGET_USER="${SUDO_USER:-ecu}"
IFACE="can0"

TX_ID="0x18DA30F9"
RX_ID="0x18DAF930"
DID="0xF190"

if [[ ! -x "$PROBE" ]]; then
  echo "ERROR: missing Stage I probe binary: $PROBE"
  echo "Run ./scripts/validate_stage_i_uds.sh first."
  exit 1
fi

cleanup() {
  ip link set "$IFACE" down >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "[ECU V2] Stage I physical SAC read-only gate"
echo "Interface: $IFACE"
echo "Bitrate: $BITRATE"
echo "Addressing: tester->SAC $TX_ID, SAC->tester $RX_ID"
echo "Request: UDS 0x22 ReadDataByIdentifier DID F190 (VIN)"
echo "No session change, reset, write, security, routine, output-control or flash command is used."

ip link set "$IFACE" down
ip link set "$IFACE" type can   bitrate "$BITRATE"   fd off   listen-only off
ip link set "$IFACE" up

echo
echo "=== CAN state before read-only UDS probe ==="
ip -details -statistics link show "$IFACE"

echo
echo "=== One read-only UDS request as user $TARGET_USER ==="
runuser -u "$TARGET_USER" --   "$PROBE"   "$IFACE"   "$BITRATE"   "$TX_ID"   "$RX_ID"   "$DID"

echo
echo "=== CAN state after read-only UDS probe ==="
ip -details -statistics link show "$IFACE"

echo
echo "STAGE_I_SAC_READ_GATE=PASS"
echo "The interface will now be returned to DOWN by the cleanup trap."
