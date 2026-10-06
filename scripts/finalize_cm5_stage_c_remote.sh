#!/usr/bin/env bash
set -euo pipefail

USER_NAME="${SUDO_USER:-$USER}"
HOME_DIR="$(getent passwd "$USER_NAME" | cut -d: -f6)"
UID_NUM="$(id -u "$USER_NAME")"
SERVICE_DIR="$HOME_DIR/.config/systemd/user"
SERVICE_FILE="$SERVICE_DIR/desktop-commander-remote.service"
DC_BIN="$HOME_DIR/.local/bin/desktop-commander"

echo "[ECU V2] Stage C2 — finalize persistent remote maintenance"

if ! command -v tailscale >/dev/null 2>&1; then
  echo "FAIL: tailscale is not installed"
  exit 1
fi

if ! command -v node >/dev/null 2>&1; then
  echo "FAIL: node is not installed"
  exit 1
fi

NODE_MAJOR="$(node -p 'process.versions.node.split(".")[0]')"
if [[ "$NODE_MAJOR" -lt 22 ]]; then
  echo "FAIL: Node.js >=22 required, found $(node -v)"
  exit 1
fi

if [[ ! -x "$DC_BIN" ]]; then
  echo "FAIL: Desktop Commander binary not found at $DC_BIN"
  exit 1
fi

if ! sudo tailscale status >/dev/null 2>&1; then
  echo "FAIL: Tailscale is not authenticated/connected. Run: sudo tailscale up"
  exit 1
fi

echo "Configuring Tailscale operator for $USER_NAME..."
sudo tailscale set --operator="$USER_NAME"

echo "Enabling user linger for persistent user services..."
sudo loginctl enable-linger "$USER_NAME"

mkdir -p "$SERVICE_DIR"

cat > "$SERVICE_FILE" <<EOF
[Unit]
Description=Desktop Commander Remote MCP
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
Environment=HOME=$HOME_DIR
Environment=PATH=$HOME_DIR/.local/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
ExecStart=$DC_BIN remote
Restart=always
RestartSec=5

[Install]
WantedBy=default.target
EOF

export XDG_RUNTIME_DIR="/run/user/$UID_NUM"
export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$UID_NUM/bus"

if [[ ! -S "$XDG_RUNTIME_DIR/bus" ]]; then
  echo "FAIL: user systemd bus not available at $XDG_RUNTIME_DIR/bus"
  echo "Log out/in once or reboot, then rerun Stage C2."
  exit 1
fi

systemctl --user daemon-reload
systemctl --user enable --now desktop-commander-remote.service

sleep 3

echo
echo "=== Tailscale ==="
tailscale status

echo
echo "=== Desktop Commander service ==="
systemctl --user is-enabled desktop-commander-remote.service
systemctl --user is-active desktop-commander-remote.service
systemctl --user status desktop-commander-remote.service --no-pager -n 20

echo
echo "=== Linger ==="
loginctl show-user "$USER_NAME" -p Linger

echo
echo "STAGE_C_REMOTE=PASS"
