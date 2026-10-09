#!/usr/bin/env bash
# One operator-approved CM5 cutover: fixed read-only SAC backend + kiosk UI.
# Installed API V1 and Bench Agent are never restarted or reconfigured.
set -Eeuo pipefail
umask 077
repo=/home/ecu/ECU_WEBGUI_PARAMS_V1
api_repo=/home/ecu/ECU_API_PARAMS_V1
base=/opt/ecu-platform/webgui
libexec=/usr/local/libexec/ecu-platform-v2
unit=/etc/systemd/system/ecu-sac-connect-v1.service
backup=""
armed=0
rollback_on_error() {
  local status=$?
  trap - EXIT ERR
  if (( status != 0 )); then
    echo "SAC_CONNECT_DEPLOY=FAIL exit=$status" >&2
    if (( armed == 1 )); then
      echo SAC_CONNECT_ROLLBACK=START >&2
      if [[ -n "$backup" && -x "$backup/rollback.sh" ]]; then
        bash "$backup/rollback.sh" || echo SAC_CONNECT_ROLLBACK=FAILED >&2
      fi
    fi
  fi
}
trap rollback_on_error EXIT
[[ "$EUID" -eq 0 && -t 0 && "${SUDO_USER:-}" == ecu ]] || {
  echo SAC_CONNECT_DEPLOY=DENIED interactive-ecu-operator-required; exit 77;
}
[[ "$(hostname -s)" == ecu ]] || exit 2
[[ "$(runuser -u ecu -- git -C "$repo" branch --show-current)" == webgui/sac-connect-identify-20261009 ]] || exit 2
[[ -z "$(runuser -u ecu -- git -C "$repo" status --porcelain)" ]] || exit 2
[[ "$(runuser -u ecu -- git -C "$api_repo" rev-parse --short=12 HEAD)" == ec1e2c3aa04e ]] || exit 2
[[ -z "$(runuser -u ecu -- git -C "$api_repo" status --porcelain)" ]] || exit 2
[[ -L "$base/current" && -f "$base/static_server.py" ]] || exit 2
[[ ! -e "$unit" && ! -e "$libexec/sac_identify_server.py" &&
   ! -e "$libexec/sac-identity-500k-probe" &&
   ! -e "$libexec/sac-parameters-500k-probe" ]] || {
  echo SAC_CONNECT_DEPLOY=ALREADY_INSTALLED; exit 2;
}
[[ ! -L "$libexec" && "$(stat -c %u "$libexec")" == 0 ]] || exit 2
[[ "$(systemctl show -p User --value ecu-kiosk.service)" == ecu-kiosk ]] || exit 2
for service in ecu-kiosk ecu-webgui-static ecu-api-v1 ecu-platform-v2-bench-agent; do
  systemctl is-active --quiet "$service" || exit 2
done
ip -details link show can0 | grep -q 'state DOWN' || exit 2
[[ "$(curl -s --max-time 4 -o /dev/null -w '%{http_code}' http://127.0.0.1:8878/api/v1/about)" == 401 ]] || exit 2
[[ "$(curl -s --max-time 4 -o /dev/null -w '%{http_code}' http://127.0.0.1:8877/)" == 200 ]] || exit 2

source_binary="$api_repo/build/params-linux/src/api/ecu_api_http"
identity_probe="$api_repo/build/params-linux/tests/ecu_daf_sac_core_v2_probe"
params_probe="$api_repo/build/params-linux/tests/ecu_daf_sac_stage42_read_probe"
stamp="$api_repo/build/params-linux/api_parameters_candidate.sha"
read -r revision digest < "$stamp"
[[ "$revision" == ec1e2c3aa04e && "$digest" =~ ^[0-9a-f]{64}$ ]] || exit 2
[[ "$digest" == "$(sha256sum "$source_binary" | cut -d' ' -f1)" ]] || exit 2
[[ -x "$identity_probe" && -x "$params_probe" ]] || exit 2
python3 -m py_compile "$repo/deploy/sac_connect/sac_identify_server.py" "$repo/deploy/webgui/static_server.py"
node --check "$repo/webgui/src/app.mjs"
node --check "$repo/webgui/src/api-client.mjs"
node --check "$repo/webgui/src/sac-connect-flow.mjs"
python3 -m unittest discover -s "$repo/tests" -p test_sac_connect_adapter.py -q
node --test "$repo"/webgui/tests/*.test.mjs > /dev/null
# Explicit preflight: do not bind or otherwise change an active CAN device.
old="$(readlink "$base/current")"
[[ "$old" == releases/* && -d "$base/$old" ]] || exit 2
revision="$(runuser -u ecu -- git -C "$repo" rev-parse --short=12 HEAD)"
release="$base/releases/$revision"
[[ ! -e "$release" && ! -L "$release" ]] || exit 2
[[ -f /var/lib/ecu-platform-v2/api-readouts/dtc-latest.v1 ]] || exit 2
dtc_sha="$(sha256sum /var/lib/ecu-platform-v2/api-readouts/dtc-latest.v1 | cut -d' ' -f1)"

install -d -o root -g root -m 0700 /var/backups/ecu-platform-v2-sac-connect
backup="$(mktemp -d /var/backups/ecu-platform-v2-sac-connect/pre-XXXXXXXX)"
printf '%s\n' "$old" > "$backup/previous-link"
cp -a "$base/static_server.py" "$backup/static_server.py"
cat > "$backup/rollback.sh" <<'RECOVERY'
#!/usr/bin/env bash
set -Eeuo pipefail
base=/opt/ecu-platform/webgui
backup="$(dirname "$(readlink -f "$0")")"
old="$(cat "$backup/previous-link")"
[[ "$old" == releases/* && -d "$base/$old" ]] || exit 2
systemctl disable --now ecu-sac-connect-v1.service >/dev/null 2>&1 || true
rm -f /etc/systemd/system/ecu-sac-connect-v1.service
rm -f /usr/local/libexec/ecu-platform-v2/sac_identify_server.py
rm -f /usr/local/libexec/ecu-platform-v2/sac-identity-500k-probe
rm -f /usr/local/libexec/ecu-platform-v2/sac-parameters-500k-probe
cp -a "$backup/static_server.py" "$base/.static-sac-recover-$$"
mv -f "$base/.static-sac-recover-$$" "$base/static_server.py"
ln -s "$old" "$base/.current-sac-recover-$$"
mv -Tf "$base/.current-sac-recover-$$" "$base/current"
systemctl daemon-reload
systemctl restart ecu-webgui-static.service ecu-kiosk.service
systemctl is-active --quiet ecu-webgui-static.service
systemctl is-active --quiet ecu-kiosk.service
ip -details link show can0 | grep -q 'state DOWN'
echo SAC_CONNECT_ROLLBACK=PASS
RECOVERY
chmod 0700 "$backup/rollback.sh"
printf '#!/usr/bin/env bash\nexec /usr/bin/bash %q\n' "$backup/rollback.sh" > /usr/local/sbin/ecu-sac-connect-rollback
chmod 0700 /usr/local/sbin/ecu-sac-connect-rollback
armed=1

install -d -o root -g root -m 0755 "$release/src/locales"
for asset in index.html styles.css src/app.mjs src/api-client.mjs \
             src/sac-connect-flow.mjs src/i18n.mjs src/domain-text.mjs \
             src/locales/en.mjs src/locales/pl.mjs; do
  install -o root -g root -m 0644 "$repo/webgui/$asset" "$release/$asset"
done
install -o root -g root -m 0755 "$repo/deploy/sac_connect/sac_identify_server.py" "$libexec/sac_identify_server.py"
install -o root -g root -m 0755 "$identity_probe" "$libexec/sac-identity-500k-probe"
install -o root -g root -m 0755 "$params_probe" "$libexec/sac-parameters-500k-probe"
install -o root -g root -m 0644 "$repo/deploy/sac_connect/ecu-sac-connect-v1.service" "$unit"
install -o root -g root -m 0644 "$repo/deploy/webgui/static_server.py" "$base/.next-sac-server-$$"
mv -f "$base/.next-sac-server-$$" "$base/static_server.py"
ln -s "releases/$revision" "$base/.next-sac-link-$$"
mv -Tf "$base/.next-sac-link-$$" "$base/current"
systemctl daemon-reload
systemctl enable --now ecu-sac-connect-v1.service
systemctl restart ecu-webgui-static.service ecu-kiosk.service

systemctl is-active --quiet ecu-sac-connect-v1.service
for service in ecu-api-v1 ecu-platform-v2-bench-agent ecu-webgui-static ecu-kiosk; do
  systemctl is-active --quiet "$service" || exit 2
done
for path in / /src/app.mjs /src/api-client.mjs /src/sac-connect-flow.mjs; do
  [[ "$(curl -s --max-time 4 -o /dev/null -w '%{http_code}' "http://127.0.0.1:8877$path")" == 200 ]] || exit 2
done
curl -fsSI --max-time 4 http://127.0.0.1:8877/ | grep -q "http://127.0.0.1:8879"
[[ "$(curl -s --max-time 4 -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:8879/api/v1/bench/daf-sac/connect -H 'Host: 127.0.0.1:8879')" == 401 ]] || exit 2
[[ "$(curl -s --max-time 4 -o /dev/null -w '%{http_code}' http://127.0.0.1:8878/api/v1/about)" == 401 ]] || exit 2
[[ "$(sha256sum /var/lib/ecu-platform-v2/api-readouts/dtc-latest.v1 | cut -d' ' -f1)" == "$dtc_sha" ]] || exit 2
ip -details link show can0 | grep -q 'state DOWN'
armed=0
trap - EXIT ERR
echo SAC_CONNECT_DEPLOY=PASS
echo SAC_CONNECT_RELEASE="$revision"
echo SAC_CONNECT_CAN=UNCHANGED_DOWN
echo SAC_CONNECT_DTC=UNCHANGED
echo SAC_CONNECT_ROLLBACK="sudo /usr/local/sbin/ecu-sac-connect-rollback"
