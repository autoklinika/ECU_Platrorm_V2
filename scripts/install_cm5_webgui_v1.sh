#!/usr/bin/env bash
# Root-gated, fail-closed Stage D -> WebGUI V1 kiosk cutover on CM5.
set -Eeuo pipefail
umask 077

repo="/home/ecu/ECU_WebGUI_Home_V1"
payload="$repo/deploy/webgui"
source_ui="$repo/webgui"
unit_dir="/etc/systemd/system"
backup=""
rollback_armed=0

failed() {
  local code="$?"
  trap - ERR
  echo "ECU_WEBGUI_INSTALL=FAIL exit=$code" >&2
  if [[ "$rollback_armed" == 1 ]]; then
    echo "ECU_WEBGUI_AUTO_ROLLBACK=START" >&2
    /usr/bin/bash /usr/local/libexec/ecu-platform/rollback_kiosk.sh "$backup" || \
      echo "ECU_WEBGUI_AUTO_ROLLBACK=FAILED - MANUAL RECOVERY REQUIRED: sudo /usr/local/sbin/ecu-webgui-rollback" >&2
  fi
  exit "$code"
}
trap failed ERR

[[ "$EUID" -eq 0 ]] || { echo "ECU_WEBGUI_INSTALL=NEEDS_ROOT"; exit 77; }
[[ "$(hostname -s)" == "ecu" ]] || { echo "ECU_WEBGUI_INSTALL=WRONG_HOST"; exit 2; }
[[ -x /usr/bin/cage && -x /usr/bin/chromium && -x /usr/bin/python3 ]] || exit 2
[[ -r "$unit_dir/ecu-kiosk.service" && -r /etc/default/ecu-kiosk ]] || exit 2
[[ -d "$source_ui/src/locales" && -r "$payload/ecu-kiosk-v1.service" ]] || exit 2

if grep -q '^User=ecu-kiosk$' "$unit_dir/ecu-kiosk.service"; then
  echo "ECU_WEBGUI_INSTALL=ALREADY_INSTALLED (validate existing cutover)"
  exit 0
fi
grep -q '^User=ecu$' "$unit_dir/ecu-kiosk.service" || {
  echo "Unexpected kiosk configuration - refusing cutover"; exit 2;
}
test "$(runuser -u ecu -- git -C "$repo" branch --show-current)" = "webgui/home-v1-i18n-20261008"
test -z "$(runuser -u ecu -- git -C "$repo" status --porcelain)"
for group in video render; do getent group "$group" >/dev/null; done

# Never disrupt an unknown process listening on the proposed loopback port.
if ss -ltn | grep -Eq ':8877[[:space:]]'; then
  echo "Port 8877 is occupied"; exit 2
fi

touch_device=""
touch_count=0
for event in /dev/input/event*; do
  [[ -e "$event" ]] || continue
  details="$(udevadm info --query=property --name="$event" 2>/dev/null || true)"
  if grep -qx "ID_VENDOR_ID=0712" <<< "$details" &&
     grep -qx "ID_MODEL_ID=0009" <<< "$details" &&
     grep -qx "ID_INPUT_TOUCHSCREEN=1" <<< "$details"; then
    touch_device="$event"
    touch_count=$((touch_count+1))
  fi
done
[[ "$touch_count" -eq 1 ]] || {
  echo "WaveShare 0712:0009 touch device not unique: $touch_count"; exit 2;
}
echo "ECU_WEBGUI_PREFLIGHT_TOUCH=$touch_device"

if ! id ecu-kiosk >/dev/null 2>&1; then
  useradd --system --user-group --home-dir /var/lib/ecu-kiosk \
    --create-home --shell /usr/sbin/nologin ecu-kiosk
fi
[[ "$(id -gn ecu-kiosk)" == "ecu-kiosk" ]] || exit 2
[[ "$(getent passwd ecu-kiosk | cut -d: -f6)" == "/var/lib/ecu-kiosk" ]] || exit 2
[[ "$(getent passwd ecu-kiosk | cut -d: -f7)" == "/usr/sbin/nologin" ]] || exit 2
[[ "$(id -nG ecu-kiosk)" == "ecu-kiosk" ]] || {
  echo "Existing kiosk user has unexpected supplemental groups"; exit 2;
}
install -d -o ecu-kiosk -g ecu-kiosk -m 0700 /var/lib/ecu-kiosk
install -d -o ecu-kiosk -g ecu-kiosk -m 0700 /var/lib/ecu-kiosk/chromium-profile

# Root-owned, immutable static assets; kiosk does not need source/worktree access.
revision="$(runuser -u ecu -- git -C "$repo" rev-parse --short=12 HEAD)"
release="/opt/ecu-platform/webgui/releases/$revision"
install -d -o root -g root -m 0755 "$release/src/locales"
for asset in index.html styles.css src/app.mjs src/i18n.mjs \
             src/domain-text.mjs src/locales/en.mjs src/locales/pl.mjs; do
  install -o root -g root -m 0644 "$source_ui/$asset" "$release/$asset"
done

# Exact, offline recovery of the previously working Stage D kiosk.
install -d -o root -g root -m 0700 /var/backups/ecu-platform-kiosk
backup="$(mktemp -d /var/backups/ecu-platform-kiosk/pre-webgui-v1-XXXXXXXX)"
cp -a "$unit_dir/ecu-kiosk.service" "$backup/ecu-kiosk.service"
cp -a /etc/default/ecu-kiosk "$backup/ecu-kiosk.default"
for filename in ecu-webgui-static.service; do
  if [[ -f "$unit_dir/$filename" ]]; then
    cp -a "$unit_dir/$filename" "$backup/$filename"
  fi
done
if [[ -f /etc/udev/rules.d/91-ecu-kiosk-touch.rules ]]; then
  cp -a /etc/udev/rules.d/91-ecu-kiosk-touch.rules "$backup/"
fi
if [[ -L /opt/ecu-platform/webgui/current ]]; then
  readlink /opt/ecu-platform/webgui/current > "$backup/webgui-current.link"
elif [[ -e /opt/ecu-platform/webgui/current ]]; then
  echo "Refusing to replace non-symlink /opt WebGUI current path"; exit 2
fi

# The rollback helper remains root-owned and works even when the source tree
# or the WebGUI HTTP service becomes unavailable.
install -D -o root -g root -m 0755 "$payload/rollback_kiosk.sh" \
  /usr/local/libexec/ecu-platform/rollback_kiosk.sh
printf '#!/usr/bin/env bash\nexec /usr/bin/bash /usr/local/libexec/ecu-platform/rollback_kiosk.sh %q\n' "$backup" \
  > /usr/local/sbin/ecu-webgui-rollback
chown root:root /usr/local/sbin/ecu-webgui-rollback
chmod 0700 /usr/local/sbin/ecu-webgui-rollback
rollback_armed=1

install -D -o root -g root -m 0644 "$payload/ecu-webgui-static.service" \
  "$unit_dir/ecu-webgui-static.service"
install -D -o root -g root -m 0644 "$payload/ecu-kiosk-v1.service" \
  "$unit_dir/ecu-kiosk.service"
install -D -o root -g root -m 0755 "$payload/ecu-kiosk-v1-launcher" \
  /usr/local/libexec/ecu-platform/ecu-kiosk-v1-launcher
install -D -o root -g root -m 0755 "$payload/ecu-kiosk-security-preflight.py" \
  /usr/local/libexec/ecu-platform/ecu-kiosk-security-preflight.py
install -D -o root -g root -m 0644 "$payload/static_server.py" \
  /opt/ecu-platform/webgui/static_server.py
install -D -o root -g root -m 0644 "$payload/91-ecu-kiosk-touch.rules" \
  /etc/udev/rules.d/91-ecu-kiosk-touch.rules
printf '%s\n' '# Static, offline WebGUI V1 only; no Core/Bench/API calls' \
  'ECU_KIOSK_URL=http://127.0.0.1:8877/' > /etc/default/ecu-kiosk
chmod 0644 /etc/default/ecu-kiosk
ln -sfn "releases/$revision" /opt/ecu-platform/webgui/current

udevadm control --reload
udevadm trigger --action=change --subsystem-match=input
udevadm settle

if [[ "$(stat -c %G "$touch_device")" != "ecu-kiosk" ]]; then
  echo "ECU_WEBGUI_TOUCH_PERMISSIONS=FAIL"
  exit 1
fi
runuser -u ecu-kiosk -- test -r "$touch_device"

systemctl daemon-reload
systemctl enable --now ecu-webgui-static.service

web_ok=0
for _ in $(seq 1 30); do
  if curl --fail --silent --show-error --max-time 2 \
      http://127.0.0.1:8877/ 2>/dev/null | grep -q 'Ecu Bench Platform'; then
    web_ok=1
    break
  fi
  sleep 0.3
done
[[ "$web_ok" == 1 ]] || { echo "ECU_WEBGUI_LOOPBACK_HTTP=FAIL"; exit 1; }
curl -fsSI http://127.0.0.1:8877/ | grep -qi '^Content-Security-Policy:'
[[ "$(curl -s -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:8877/)" == 405 ]] || exit 1
[[ "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8877/../../etc/passwd)" == 404 ]] || exit 1

echo "ECU_WEBGUI_HTTP=PASS"
echo "ECU_WEBGUI_CUTOVER=START"
systemctl enable ecu-kiosk.service
systemctl restart ecu-kiosk.service

kiosk_ok=0
for _ in $(seq 1 30); do
  main_pid="$(systemctl show -p MainPID --value ecu-kiosk.service)"
  if systemctl is-active --quiet ecu-kiosk.service &&
     [[ "$main_pid" =~ ^[0-9]+$ ]] && [[ "$main_pid" -gt 0 ]] &&
     [[ "$(ps -p "$main_pid" -o user= | xargs)" == "ecu-kiosk" ]] &&
     pgrep -u ecu-kiosk -x chromium >/dev/null; then
    sleep 2
    if systemctl is-active --quiet ecu-kiosk.service; then
      kiosk_ok=1
      break
    fi
  fi
  sleep 1
done
[[ "$kiosk_ok" == 1 ]] || {
  journalctl -u ecu-kiosk.service --no-pager -n 30 >&2 || true
  echo "ECU_WEBGUI_KIOSK_START=FAIL"
  exit 1
}

runuser -u ecu-kiosk -- test -r "$touch_device"
if runuser -u ecu-kiosk -- test -w /run/ecu-platform-v2-bench/request.sock; then
  echo "ECU_WEBGUI_BENCH_AGENT_ISOLATION=FAIL"
  exit 1
fi
if runuser -u ecu-kiosk -- test -r /home/ecu/ECU_Platrorm_V2/CMakeLists.txt; then
  echo "ECU_WEBGUI_PROJECT_ISOLATION=FAIL"
  exit 1
fi
if ! journalctl -u ecu-kiosk.service --no-pager -n 80 | grep -q 'ECU_KIOSK_SECURITY_PREFLIGHT=PASS'; then
  echo "ECU_WEBGUI_SECURITY_PREFLIGHT_LOG=FAIL"
  exit 1
fi

echo "ECU_WEBGUI_INSTALL=PASS"
echo "ECU_WEBGUI_RELEASE=$revision"
echo "ECU_WEBGUI_STATIC_URL=http://127.0.0.1:8877/"
echo "ECU_WEBGUI_USER=ecu-kiosk"
echo "ECU_WEBGUI_TOUCH_PERMISSIONS=PASS"
echo "ECU_WEBGUI_BENCH_AGENT_ISOLATION=PASS"
echo "ECU_WEBGUI_RECOVERY=sudo /usr/local/sbin/ecu-webgui-rollback"
echo "KIOSK_AUTO_START=enabled (reboot test and human touchscreen acceptance still required)"
echo "CORE_AND_API=unchanged"
trap - ERR
