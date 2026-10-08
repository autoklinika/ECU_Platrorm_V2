#!/usr/bin/env bash
set -euo pipefail

USER_NAME="${SUDO_USER:-$USER}"
HOME_DIR="$(getent passwd "$USER_NAME" | cut -d: -f6)"

echo "[ECU V2] Stage C1 — install remote maintenance stack"
echo "User: $USER_NAME"
echo "Home: $HOME_DIR"

echo
echo "=== Tailscale ==="
if ! command -v tailscale >/dev/null 2>&1; then
  curl -fsSL https://tailscale.com/install.sh -o /tmp/tailscale-install.sh
  sudo sh /tmp/tailscale-install.sh
else
  echo "Tailscale already installed."
fi

sudo systemctl enable --now tailscaled

echo
echo "=== Node.js 22 ==="
NODE_MAJOR=0
if command -v node >/dev/null 2>&1; then
  NODE_MAJOR="$(node -p 'process.versions.node.split(".")[0]' 2>/dev/null || echo 0)"
fi

if [[ "$NODE_MAJOR" -lt 22 ]]; then
  curl -fsSL https://deb.nodesource.com/setup_22.x -o /tmp/nodesource_setup_22.x.sh
  sudo -E bash /tmp/nodesource_setup_22.x.sh
  sudo apt install -y nodejs
else
  echo "Node.js $(node -v) already satisfies >=22."
fi

echo
echo "=== Remote Desktop Commander ==="
mkdir -p "$HOME_DIR/.local"
npm install -g --prefix "$HOME_DIR/.local" @wonderwhy-er/desktop-commander@latest

echo
echo "=== Versions / state ==="
tailscale version | head -1 || true
node -v
npm -v
test -x "$HOME_DIR/.local/bin/desktop-commander"
echo "desktop-commander binary: $HOME_DIR/.local/bin/desktop-commander"
systemctl is-enabled tailscaled
systemctl is-active tailscaled

echo
echo "[ECU V2] Stage C1 complete"
echo
echo "NEXT — authenticate Tailscale:"
echo "  sudo tailscale up"
echo
echo "Then pair Remote Desktop Commander once:"
echo "  $HOME_DIR/.local/bin/desktop-commander remote"
echo
echo "After it reports CONNECTED, press Ctrl+C once to stop the foreground pairing process."
echo "Then run Stage C2 finalize."
