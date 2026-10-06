#!/usr/bin/env bash
set -euo pipefail

TARGET_USER="${1:-ecu}"
TARGET_UID="$(id -u "$TARGET_USER")"
FAIL=0

pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*"; FAIL=1; }

echo "[ECU V2] Stage D — kiosk validation"

echo
echo "=== DRM/KMS ==="
if [[ -e /dev/dri/card0 && -e /dev/dri/renderD128 ]]; then
  pass "/dev/dri card/render nodes present"
else
  fail "DRM nodes missing"
fi

if grep -Eq '^[[:space:]]*dtoverlay=vc4-kms-v3d([,[:space:]]|$)' /boot/firmware/config.txt; then
  pass "vc4-kms-v3d configured"
else
  fail "vc4-kms-v3d not found in boot config"
fi

echo
echo "=== Packages ==="
for pkg in cage chromium rpi-chromium-mods; do
  if dpkg-query -W -f='${Status} ${Version}\n' "$pkg" 2>/dev/null | grep -q '^install ok installed '; then
    dpkg-query -W -f='PASS: ${Package} ${Version}\n' "$pkg"
  else
    fail "$pkg not installed"
  fi
done

echo
echo "=== systemd ==="
if [[ "$(systemctl is-enabled ecu-kiosk.service 2>/dev/null || true)" == "enabled" ]]; then
  pass "ecu-kiosk.service enabled"
else
  fail "ecu-kiosk.service not enabled"
fi

if [[ "$(systemctl is-active ecu-kiosk.service 2>/dev/null || true)" == "active" ]]; then
  pass "ecu-kiosk.service active"
else
  fail "ecu-kiosk.service not active"
fi

if [[ "$(systemctl get-default)" == "graphical.target" ]]; then
  pass "default target is graphical.target"
else
  fail "default target is not graphical.target"
fi

echo
echo "=== Wayland / Chromium process contract ==="
if ps -eo args= | grep -E '[c]age -- /usr/bin/chromium' >/dev/null; then
  pass "Cage owns Chromium as kiosk client"
else
  fail "Cage -> Chromium process chain not found"
fi

if ps -eo args= | grep -E '[c]hromium .*--ozone-platform=wayland' >/dev/null; then
  pass "Chromium forced to Wayland/Ozone"
else
  fail "Chromium Wayland/Ozone flag not found"
fi

if ps -eo args= | grep -E '[c]hromium .*--kiosk' >/dev/null; then
  pass "Chromium kiosk mode active"
else
  fail "Chromium --kiosk flag not found"
fi

if pgrep -x Xwayland >/dev/null 2>&1; then
  fail "Xwayland process is running; Stage D baseline must stay native Wayland"
else
  pass "no Xwayland process"
fi

if find "/run/user/$TARGET_UID" -maxdepth 1 -type s -name 'wayland-*' -print -quit 2>/dev/null | grep -q .; then
  pass "Wayland socket present in /run/user/$TARGET_UID"
else
  fail "Wayland socket not found"
fi

echo
echo "=== Kiosk URL ==="
if grep -q '^ECU_KIOSK_URL=' /etc/default/ecu-kiosk; then
  grep '^ECU_KIOSK_URL=' /etc/default/ecu-kiosk
  pass "kiosk URL configured outside Core"
else
  fail "/etc/default/ecu-kiosk missing ECU_KIOSK_URL"
fi

echo
echo "=== Input visibility (advisory) ==="
grep -i -A6 -B2 -E 'waveshare|touchscreen|touch screen' /proc/bus/input/devices || true

echo
echo "=== Recent service log ==="
journalctl -u ecu-kiosk.service -n 60 --no-pager || true

if [[ "$FAIL" -ne 0 ]]; then
  echo
  echo "STAGE_D_KIOSK=FAIL"
  exit 1
fi

echo
echo "STAGE_D_KIOSK=PASS"
