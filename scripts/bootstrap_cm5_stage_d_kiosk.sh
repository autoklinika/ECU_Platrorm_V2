#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "ERROR: Stage D installation requires root."
  echo "Run: sudo $0"
  exit 1
fi

TARGET_USER="${SUDO_USER:-ecu}"
TARGET_HOME="$(getent passwd "$TARGET_USER" | cut -d: -f6)"

if [[ -z "$TARGET_HOME" || ! -d "$TARGET_HOME" ]]; then
  echo "ERROR: cannot resolve home directory for user '$TARGET_USER'."
  exit 1
fi

echo "[ECU V2] Stage D — minimal DRM/KMS + Wayland + Cage + Chromium kiosk"
echo "Target user: $TARGET_USER"
echo "Target home: $TARGET_HOME"

echo
echo "=== Preflight: DRM/KMS ==="
test -e /dev/dri/card0
test -e /dev/dri/renderD128
grep -Eq '^[[:space:]]*dtoverlay=vc4-kms-v3d([,[:space:]]|$)' /boot/firmware/config.txt
echo "KMS preflight: PASS"

echo
echo "=== Install minimal kiosk runtime ==="
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends   cage   chromium   rpi-chromium-mods

echo
echo "=== Install ECU kiosk launcher and placeholder ==="
install -d -m 0755 /usr/local/libexec/ecu-platform
install -d -m 0755 /opt/ecu-platform/kiosk
install -d -m 0755 /etc/default
install -d -o "$TARGET_USER" -g "$TARGET_USER" -m 0700   "$TARGET_HOME/.local/state/ecu-platform/chromium-kiosk"

cat >/usr/local/libexec/ecu-platform/ecu-kiosk-launcher <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

: "${ECU_KIOSK_URL:=file:///opt/ecu-platform/kiosk/stage-d.html}"

UID_NUM="$(id -u)"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/${UID_NUM}}"
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=cage

PROFILE_DIR="${ECU_KIOSK_PROFILE_DIR:-${HOME}/.local/state/ecu-platform/chromium-kiosk}"
mkdir -p "$PROFILE_DIR"

exec /usr/bin/cage -- /usr/bin/chromium   --ozone-platform=wayland   --kiosk   --no-first-run   --no-default-browser-check   --disable-session-crashed-bubble   --password-store=basic   --user-data-dir="$PROFILE_DIR"   "$ECU_KIOSK_URL"
EOF
chmod 0755 /usr/local/libexec/ecu-platform/ecu-kiosk-launcher

cat >/opt/ecu-platform/kiosk/stage-d.html <<'EOF'
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ECU Platform V2 — Stage D</title>
<style>
html,body{height:100%;margin:0;background:#111;color:#eee;font-family:system-ui,sans-serif}
body{display:grid;place-items:center}
main{max-width:900px;padding:48px;text-align:center}
h1{font-size:48px;margin:0 0 18px}
p{font-size:22px;line-height:1.45;color:#bbb}
code{color:#fff}
</style>
</head>
<body>
<main>
<h1>ECU Platform V2</h1>
<p>Stage D kiosk runtime is active.</p>
<p><code>DRM/KMS → Wayland/Cage → Chromium</code></p>
<p>This is a temporary local placeholder. It does not select the final WebGUI framework or HTTP/WebSocket backend.</p>
</main>
</body>
</html>
EOF

cat >/etc/default/ecu-kiosk <<'EOF'
# Stage D default: local framework-neutral placeholder.
# Replace only this URL when the ECU Platform WebGUI endpoint is ready.
ECU_KIOSK_URL=file:///opt/ecu-platform/kiosk/stage-d.html
EOF

echo
echo "=== PAM session for logind ==="
cat >/etc/pam.d/ecu-kiosk <<'EOF'
auth       required pam_unix.so nullok
account    required pam_unix.so
session    required pam_unix.so
session    required pam_systemd.so
EOF
chmod 0644 /etc/pam.d/ecu-kiosk

echo
echo "=== systemd kiosk service ==="
cat >/etc/systemd/system/ecu-kiosk.service <<EOF
[Unit]
Description=ECU Platform V2 local kiosk (Cage + Chromium)
After=systemd-user-sessions.service dbus.socket systemd-logind.service
Before=graphical.target
Wants=dbus.socket systemd-logind.service
Conflicts=getty@tty1.service
After=getty@tty1.service
ConditionPathExists=/dev/tty0

[Service]
Type=simple
User=$TARGET_USER
PAMName=ecu-kiosk
UtmpIdentifier=tty1
UtmpMode=user
TTYPath=/dev/tty1
TTYReset=yes
TTYVHangup=yes
TTYVTDisallocate=yes
StandardInput=tty-fail
WorkingDirectory=$TARGET_HOME
Environment=HOME=$TARGET_HOME
EnvironmentFile=-/etc/default/ecu-kiosk
ExecStart=/usr/local/libexec/ecu-platform/ecu-kiosk-launcher
ExecStartPost=+/usr/bin/chvt 1
Restart=always
RestartSec=2
TimeoutStopSec=10

[Install]
WantedBy=graphical.target
EOF

systemctl daemon-reload
systemctl enable ecu-kiosk.service
systemctl set-default graphical.target
systemctl restart ecu-kiosk.service

sleep 2

echo
echo "=== Runtime state ==="
systemctl is-enabled ecu-kiosk.service
systemctl is-active ecu-kiosk.service
systemctl get-default
/usr/bin/cage -v
/usr/bin/chromium --version
dpkg-query -W -f='${Package} ${Version}\n' cage chromium rpi-chromium-mods

if ! systemctl is-active --quiet ecu-kiosk.service; then
  echo "ERROR: ecu-kiosk.service did not become active."
  journalctl -u ecu-kiosk.service -n 120 --no-pager || true
  exit 1
fi

echo
echo "STAGE_D_INSTALL=PASS"
echo "Next: run scripts/validate_cm5_stage_d_kiosk.sh and then perform reboot validation."
