#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "ERROR: run with sudo"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROBE="$ROOT_DIR/build/stage-g-debug/tests/ecu_socketcan_adapter_probe"
TARGET_USER="${SUDO_USER:-ecu}"
IFACE="${1:-can0}"
NOMINAL="${2:-500000}"
DATA="${3:-2000000}"

if [[ ! -x "$PROBE" ]]; then
  echo "ERROR: missing Stage G probe binary: $PROBE"
  echo "Run ./scripts/validate_stage_g_socketcan.sh first."
  exit 1
fi

cleanup() {
  ip link set "$IFACE" down >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "[ECU V2] Stage G2 — physical SocketCAN listen-only gate"
echo "Interface: $IFACE"
echo "Profile: nominal=$NOMINAL data=$DATA FD=on listen-only=on"

ip link set "$IFACE" down
ip link set "$IFACE" type can \
  bitrate "$NOMINAL" \
  dbitrate "$DATA" \
  fd on \
  listen-only on
ip link set "$IFACE" up

echo
echo "=== Kernel state before adapter probe ==="
ip -details -statistics link show "$IFACE"

echo
echo "=== Adapter probe as user $TARGET_USER ==="
runuser -u "$TARGET_USER" -- "$PROBE" "$IFACE" "$NOMINAL" "$DATA"

echo
echo "=== Kernel state after adapter probe ==="
ip -details -statistics link show "$IFACE"

echo
echo "STAGE_G2_LISTEN_ONLY=PASS"
echo "The interface will now be returned to DOWN by the cleanup trap."
