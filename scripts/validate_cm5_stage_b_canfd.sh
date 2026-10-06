#!/usr/bin/env bash
set -euo pipefail

echo "[ECU V2] Stage B2 — validate CAN/CAN-FD hardware"

echo "=== kernel ==="
uname -a

echo "=== CAN driver/module ==="
lsmod | grep -E 'mcp251xfd|can_dev' || true

echo "=== can0 ==="
if ! ip link show can0 >/dev/null 2>&1; then
  echo "FAIL: can0 not present"
  echo
  echo "Relevant kernel messages:"
  dmesg | grep -Ei 'mcp251|spi|can' | tail -80 || true
  exit 1
fi

ip -details link show can0

echo
echo "=== CAN-FD capability probe (no traffic transmitted) ==="
sudo ip link set can0 down || true
sudo ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on
sudo ip link set can0 up

DETAILS="$(ip -details link show can0)"
printf '%s\n' "$DETAILS"

if ! grep -q 'can <FD' <<<"$DETAILS"; then
  echo "FAIL: kernel does not report CAN-FD capability as active"
  sudo ip link set can0 down || true
  exit 1
fi

if ! grep -q 'dbitrate 2000000' <<<"$DETAILS"; then
  echo "FAIL: expected CAN-FD data bitrate is not active"
  sudo ip link set can0 down || true
  exit 1
fi

if ! grep -q 'mtu 72' <<<"$DETAILS"; then
  echo "FAIL: interface MTU is not CAN-FD MTU 72"
  sudo ip link set can0 down || true
  exit 1
fi

sudo ip link set can0 down

echo
echo "STAGE_B_CANFD=PASS"
echo "Interface returned to DOWN state. No CAN frames were transmitted."
