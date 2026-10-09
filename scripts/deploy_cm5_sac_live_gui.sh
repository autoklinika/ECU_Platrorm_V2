#!/usr/bin/env bash
# ECU V2 Issue #29 screen-scoped continuous read-only parameter polling.
# Operator-only bounded adapter+kiosk cutover; no ECU traffic during install.
set -Eeuo pipefail
umask 077
repo=/home/ecu/ECU_V2_INTEGRATION
base=/opt/ecu-platform/webgui
server="$base/static_server.py"
unit=ecu-webgui-static.service
old_release=releases/f15249ffe6ef
old_server_sha=4021ffb6e1bdacfce7a8c81b90537564918c0e0565d336014bea61e9dd44eb88
old_app_sha=eaf9c8851ab7fbc8503bdd02826c767095d282424e01e882b0f4fc1dc92e035d
old_adapter_sha=ac5a4b30ca29dd2474258a718165f029a8d70607bbbf1937d049579887f7ab95
dtc=/var/lib/ecu-platform-v2/api-readouts/dtc-latest.v1
native=/usr/local/libexec/ecu-platform-v2/sac-parameters-500k-probe
adapter=/usr/local/libexec/ecu-platform-v2/sac_identify_server.py
expected_native_sha=23ad70516fffe97bf624f45875f82da4860f78a86e8e54349094dc66ad180e63
backup=""
armed=0
phase=preflight

on_exit() {
  local rc=$?
  trap - EXIT
  if ((rc != 0)); then
    echo "SAC_LIVE_GUI_DEPLOY=FAIL phase=$phase exit=$rc" >&2
    if ((armed == 1)); then
      bash "$backup/rollback.sh" ||
        echo "SAC_LIVE_GUI_ROLLBACK=FAILED manual-recovery-required" >&2
    fi
  fi
}
trap on_exit EXIT
[[ "$EUID" == 0 && -t 0 && "${SUDO_USER:-}" == ecu ]] || {
  echo "SAC_LIVE_GUI_DEPLOY=DENIED interactive-ecu-sudo-required" >&2
  exit 77
}
[[ "$(hostname -s)" == ecu ]] || exit 2
[[ "$(runuser -u ecu -- git -C "$repo" branch --show-current)" == integration/ecu-v2-operational-candidate-20261009 ]] || exit 2
[[ -z "$(runuser -u ecu -- git -C "$repo" status --porcelain)" ]] || exit 2
revision="$(runuser -u ecu -- git -C "$repo" rev-parse --short=12 HEAD)"
release="$base/releases/$revision"
[[ ! -e "$release" && ! -L "$release" ]] || exit 2
[[ -L "$base/current" && "$(readlink "$base/current")" == "$old_release" ]] || exit 2
[[ ! -L "$server" && "$(stat -c '%a:%U:%G' "$server")" == 644:root:root ]] || exit 2
[[ "$(sha256sum "$server" | cut -d' ' -f1)" == "$old_server_sha" ]] || exit 2
[[ "$(sha256sum "$base/current/src/app.mjs" | cut -d' ' -f1)" == "$old_app_sha" ]] || exit 2
[[ "$(sha256sum "$native" | cut -d' ' -f1)" == "$expected_native_sha" ]] || exit 2
[[ -f "$adapter" && ! -L "$adapter" &&
   "$(stat -c '%a:%U:%G' "$adapter")" == 755:root:root ]] || exit 2
[[ "$(sha256sum "$adapter" | cut -d' ' -f1)" == "$old_adapter_sha" ]] || exit 2
[[ -f "$dtc" && ! -L "$dtc" ]] || exit 2
[[ "$(systemctl show "$unit" -p DynamicUser --value)" == yes ]] || exit 2
[[ -f /etc/systemd/system/ecu-webgui-static.service.d/50-ecu-prototype-credential.conf ]] || exit 2
for unit_name in ecu-api-v1 ecu-sac-connect-v1 ecu-webgui-static ecu-kiosk ecu-platform-v2-bench-agent; do
  systemctl is-active --quiet "$unit_name" || exit 2
done
ip -details link show can0 | grep -q 'state DOWN' || exit 2
[[ "$(curl -s --max-time 4 -o /dev/null -w '%{http_code}' -H 'X-ECU-Kiosk: v1' http://127.0.0.1:8877/kiosk/v1/about)" == 200 ]] || exit 2
[[ "$(curl -s --max-time 4 -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:8879/api/v1/bench/daf-sac/connect)" == 401 ]] || exit 2

phase=offline-tests
runuser -u ecu -- node --test "$repo"/webgui/tests/*.test.mjs >/dev/null
runuser -u ecu -- python3 -m unittest discover -s "$repo/tests" -p test_sac_connect_adapter.py -q
python3 -m py_compile "$repo/deploy/sac_connect/sac_identify_server.py"
runuser -u ecu -- python3 -m unittest discover -s "$repo/tests" -p test_sac_kiosk_proxy.py -q
python3 -m py_compile "$repo/deploy/webgui/static_server.py"
for asset in index.html styles.css src/app.mjs src/api-client.mjs \
             src/sac-connect-flow.mjs src/sac-parameter-monitor.mjs \
             src/i18n.mjs src/domain-text.mjs src/locales/pl.mjs src/locales/en.mjs; do
  [[ -f "$repo/webgui/$asset" && ! -L "$repo/webgui/$asset" ]] || exit 2
done
dtc_sha="$(sha256sum "$dtc" | cut -d' ' -f1)"
native_sha="$(sha256sum "$native" | cut -d' ' -f1)"
adapter_sha="$(sha256sum "$adapter" | cut -d' ' -f1)"

phase=backup
install -d -o root -g root -m 0700 /var/backups/ecu-platform-v2-sac-live-gui
backup="$(mktemp -d /var/backups/ecu-platform-v2-sac-live-gui/pre-XXXXXXXX)"
cp -a "$server" "$backup/static_server.py"
cp -a "$adapter" "$backup/sac_identify_server.py"
printf '%s\n' "$old_release" > "$backup/previous-release"
cat > "$backup/rollback.sh" <<'RECOVER'
#!/usr/bin/env bash
set -Eeuo pipefail
dir="$(dirname "$(readlink -f "$0")")"
base=/opt/ecu-platform/webgui
prev="$(cat "$dir/previous-release")"
[[ "$prev" == releases/* && -d "$base/$prev" ]] || exit 2
install -o root -g root -m 0644 "$dir/static_server.py" "$base/.restore-server-$$"
mv -f "$base/.restore-server-$$" "$base/static_server.py"
lib=/usr/local/libexec/ecu-platform-v2
install -o root -g root -m 0755 "$dir/sac_identify_server.py" "$lib/.restore-sac-$$"
mv -f "$lib/.restore-sac-$$" "$lib/sac_identify_server.py"
ln -s "$prev" "$base/.restore-live-gui-$$"
mv -Tf "$base/.restore-live-gui-$$" "$base/current"
systemctl restart ecu-sac-connect-v1.service ecu-webgui-static.service ecu-kiosk.service
for service in ecu-webgui-static ecu-kiosk ecu-api-v1 ecu-sac-connect-v1 ecu-platform-v2-bench-agent; do
  systemctl is-active --quiet "$service"
done
ip -details link show can0 | grep -q 'state DOWN'
echo "SAC_LIVE_GUI_ROLLBACK=PASS"
RECOVER
chmod 0700 "$backup/rollback.sh"
printf '#!/usr/bin/env bash\nexec /usr/bin/bash %q\n' "$backup/rollback.sh" > /usr/local/sbin/ecu-sac-live-gui-rollback
chown root:root /usr/local/sbin/ecu-sac-live-gui-rollback
chmod 0700 /usr/local/sbin/ecu-sac-live-gui-rollback
armed=1

phase=install
install -d -o root -g root -m 0755 "$release/src/locales"
for asset in index.html styles.css src/app.mjs src/api-client.mjs \
             src/sac-connect-flow.mjs src/sac-parameter-monitor.mjs \
             src/i18n.mjs src/domain-text.mjs src/locales/pl.mjs src/locales/en.mjs; do
  install -o root -g root -m 0644 "$repo/webgui/$asset" "$release/$asset"
done
install -o root -g root -m 0644 "$repo/deploy/webgui/static_server.py" "$base/.live-server-$$"
install -o root -g root -m 0755 "$repo/deploy/sac_connect/sac_identify_server.py" "$adapter.live-next-$$"
mv -f "$base/.live-server-$$" "$server"
mv -f "$adapter.live-next-$$" "$adapter"
ln -s "releases/$revision" "$base/.live-release-$$"
mv -Tf "$base/.live-release-$$" "$base/current"
systemctl restart ecu-sac-connect-v1.service ecu-webgui-static.service ecu-kiosk.service

phase=smoke
healthy=0
for i in $(seq 1 40); do
  if systemctl is-active --quiet ecu-webgui-static.service &&
     systemctl is-active --quiet ecu-kiosk.service &&
     [[ "$(curl -s --max-time 2 -o /dev/null -w '%{http_code}' -H 'X-ECU-Kiosk: v1' http://127.0.0.1:8877/kiosk/v1/about 2>/dev/null || true)" == 200 ]]; then
    healthy=1
    break
  fi
  sleep 0.25
done
[[ "$healthy" == 1 ]] || exit 2
for asset in / /src/app.mjs /src/api-client.mjs /src/sac-parameter-monitor.mjs /src/sac-connect-flow.mjs /src/locales/pl.mjs; do
  [[ "$(curl -s --max-time 4 -o /dev/null -w '%{http_code}' "http://127.0.0.1:8877$asset")" == 200 ]] || exit 2
done
[[ "$(sha256sum "$dtc" | cut -d' ' -f1)" == "$dtc_sha" ]] || exit 2
[[ "$(sha256sum "$native" | cut -d' ' -f1)" == "$native_sha" ]] || exit 2
[[ "$(sha256sum "$adapter" | cut -d' ' -f1)" == "$(sha256sum "$repo/deploy/sac_connect/sac_identify_server.py" | cut -d' ' -f1)" ]] || exit 2
[[ "$(curl -s --max-time 4 -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:8879/api/v1/bench/daf-sac/parameters/read)" == 401 ]] || exit 2
[[ "$(sha256sum "$server" | cut -d' ' -f1)" == "$(sha256sum "$repo/deploy/webgui/static_server.py" | cut -d' ' -f1)" ]] || exit 2
ip -details link show can0 | grep -q 'state DOWN' || exit 2
armed=0
trap - EXIT
echo "SAC_LIVE_GUI_DEPLOY=PASS release=$revision"
echo "SAC_LIVE_GUI_MONITOR=SCREEN_SCOPED_PARAMETERS_ONLY_NO_REIDENTIFICATION"
echo "SAC_LIVE_GUI_NATIVE=UNCHANGED"
echo "SAC_LIVE_GUI_ADAPTER=UPDATED_FIXED_SESSION"
echo "SAC_LIVE_GUI_DTC=UNCHANGED"
echo "SAC_LIVE_GUI_CAN=DOWN_NO_PROBE_DURING_INSTALL"
echo "SAC_LIVE_GUI_ROLLBACK=sudo /usr/local/sbin/ecu-sac-live-gui-rollback"
