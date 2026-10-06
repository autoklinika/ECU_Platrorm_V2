#!/usr/bin/env bash
set -euo pipefail

CONFIG=/boot/firmware/config.txt
MARKER_BEGIN="# BEGIN ECU PLATFORM V2 PROTOTYPE A - CANFD"
MARKER_END="# END ECU PLATFORM V2 PROTOTYPE A - CANFD"

if [[ ! -f "$CONFIG" ]]; then
  echo "ERROR: $CONFIG not found"
  exit 1
fi

echo "[ECU V2] Stage B1 — configure KAmod CAN-FD / MCP251xFD"

if grep -Fq "$MARKER_BEGIN" "$CONFIG"; then
  echo "CAN-FD block already present; no change."
else
  sudo cp -a "$CONFIG" "$CONFIG.pre-ecu-v2-canfd.$(date +%Y%m%d_%H%M%S).bak"
  cat <<'EOF' | sudo tee -a "$CONFIG" >/dev/null

# BEGIN ECU PLATFORM V2 PROTOTYPE A - CANFD
dtparam=spi=on
dtoverlay=mcp251xfd,spi0-0,oscillator=40000000,interrupt=25
dtoverlay=spi-bcm2835
# END ECU PLATFORM V2 PROTOTYPE A - CANFD
EOF
  echo "CAN-FD block appended to $CONFIG"
fi

echo
echo "Configured block:"
sed -n "/$MARKER_BEGIN/,/$MARKER_END/p" "$CONFIG"

echo
echo "A reboot is required before Stage B2 validation."
echo "Run: sudo reboot"
