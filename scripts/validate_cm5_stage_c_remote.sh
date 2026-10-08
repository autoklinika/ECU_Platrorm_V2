#!/usr/bin/env bash
set -euo pipefail

USER_NAME="$USER"
UID_NUM="$(id -u)"
export XDG_RUNTIME_DIR="/run/user/$UID_NUM"
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$UID_NUM/bus"

echo "[ECU V2] Stage C3 — validation"

echo "=== Tailscale service ==="
systemctl is-enabled tailscaled
systemctl is-active tailscaled

echo "=== Tailscale identity ==="
tailscale status

echo "=== Node ==="
node -v
npm -v

echo "=== Desktop Commander user service ==="
systemctl --user is-enabled desktop-commander-remote.service
systemctl --user is-active desktop-commander-remote.service

echo "=== Linger ==="
loginctl show-user "$USER_NAME" -p Linger

echo "=== Processes ==="
ps -eo pid,ppid,user,cmd | grep -E 'desktop-commander remote|tailscaled' | grep -v grep || true

if [[ "$(systemctl is-active tailscaled)" != "active" ]]; then
  echo "FAIL: tailscaled not active"
  exit 1
fi

if [[ "$(systemctl --user is-active desktop-commander-remote.service)" != "active" ]]; then
  echo "FAIL: Desktop Commander service not active"
  exit 1
fi

if [[ "$(loginctl show-user "$USER_NAME" -p Linger --value)" != "yes" ]]; then
  echo "FAIL: user linger is not enabled"
  exit 1
fi

echo
echo "STAGE_C_REMOTE=PASS"
