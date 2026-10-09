#!/usr/bin/env bash
# Prototype kiosk: no operator-entered API token. Internal credential is
# systemd-managed, remains inaccessible to browser JS. No CAN read at install.
set -Eeuo pipefail
umask 077
repo=/home/ecu/ECU_WEBGUI_PARAMS_V1
base=/opt/ecu-platform/webgui
unit_dir=/etc/systemd/system/ecu-webgui-static.service.d
unit_conf="$unit_dir/50-ecu-prototype-credential.conf"
backup=""
armed=0

rollback_error() {
  local rc=$?
  trap - EXIT
  if (( rc != 0 )); then
    echo "SAC_KIOSK_DEPLOY=FAIL code=$rc"
    if (( armed == 1 )) && [[ -x "$backup/rollback.sh" ]]; then
      bash "$backup/rollback.sh" || echo "SAC_KIOSK_ROLLBACK=FAILED"
    fi
  fi
}
trap rollback_error EXIT

[[ "$EUID" -eq 0 && -t 0 && "${SUDO_USER:-}" == ecu ]] || {
  echo "SAC_KIOSK_DEPLOY=DENIED interactive-operator-sudo-required"; exit 77;
}
[[ "$(hostname -s)" == ecu ]] || exit 2
[[ "$(runuser -u ecu -- git -C "$repo" branch --show-current)" == "webgui/sac-local-prototype-no-token-20261009" ]] || exit 2
[[ -z "$(runuser -u ecu -- git -C "$repo" status --porcelain)" ]] || exit 2
[[ -L "$base/current" && "$(readlink "$base/current")" == "releases/5e354baebe1d" ]] || exit 2
[[ ! -e "$unit_conf" && ! -L "$unit_conf" ]] || exit 2
[[ -f /etc/ecu-platform-v2/api/token && ! -L /etc/ecu-platform-v2/api/token ]] || exit 2
[[ "$(systemctl show -p DynamicUser --value ecu-webgui-static.service)" == yes ]] || exit 2
for svc in ecu-webgui-static ecu-kiosk ecu-api-v1 ecu-sac-connect-v1 ecu-platform-v2-bench-agent; do
  systemctl is-active --quiet "$svc" || exit 2
done
ip -details link show can0 | grep -q 'state DOWN' || exit 2
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' http://127.0.0.1:8878/api/v1/about)" == 401 ]] || exit 2
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:8879/api/v1/bench/daf-sac/connect)" == 401 ]] || exit 2
for asset in webgui/index.html webgui/styles.css webgui/src/app.mjs webgui/src/api-client.mjs; do
  [[ -s "$repo/$asset" ]] || exit 2
done
python3 -m py_compile "$repo/deploy/webgui/static_server.py"
node --check "$repo/webgui/src/api-client.mjs"
node --check "$repo/webgui/src/app.mjs"
node --test "$repo"/webgui/tests/*.test.mjs >/dev/null
python3 -m unittest discover -s "$repo/tests" -p test_webgui_api_deploy_contract.py -q
dtc_file=/var/lib/ecu-platform-v2/api-readouts/dtc-latest.v1
[[ -f "$dtc_file" ]] || exit 2
dtc_sha="$(sha256sum "$dtc_file" | cut -d' ' -f1)"

revision="$(runuser -u ecu -- git -C "$repo" rev-parse --short=12 HEAD)"
release="$base/releases/$revision"
[[ ! -e "$release" && ! -L "$release" ]] || exit 2
old="$(readlink "$base/current")"
install -d -o root -g root -m 0700 /var/backups/ecu-platform-v2-kiosk-auth
backup="$(mktemp -d /var/backups/ecu-platform-v2-kiosk-auth/pre-XXXXXXXX)"
printf '%s\n' "$old" > "$backup/previous-link"
cp -a "$base/static_server.py" "$backup/static_server.py"
cat > "$backup/rollback.sh" <<'RECOVER'
#!/usr/bin/env bash
set -Eeuo pipefail
base=/opt/ecu-platform/webgui
dir="$(dirname "$(readlink -f "$0")")"
old="$(cat "$dir/previous-link")"
[[ "$old" == releases/* && -d "$base/$old" ]] || exit 2
rm -f /etc/systemd/system/ecu-webgui-static.service.d/50-ecu-prototype-credential.conf
cp -a "$dir/static_server.py" "$base/.restore-kiosk-$$"
mv -f "$base/.restore-kiosk-$$" "$base/static_server.py"
ln -s "$old" "$base/.restore-kiosk-link-$$"
mv -Tf "$base/.restore-kiosk-link-$$" "$base/current"
systemctl daemon-reload
systemctl restart ecu-webgui-static.service ecu-kiosk.service
systemctl is-active --quiet ecu-webgui-static.service
systemctl is-active --quiet ecu-kiosk.service
ip -details link show can0 | grep -q 'state DOWN'
echo SAC_KIOSK_ROLLBACK=PASS
RECOVER
chmod 0700 "$backup/rollback.sh"
printf '#!/usr/bin/env bash\nexec /usr/bin/bash %q\n' "$backup/rollback.sh" > /usr/local/sbin/ecu-sac-kiosk-rollback
chmod 0700 /usr/local/sbin/ecu-sac-kiosk-rollback
armed=1

install -d -o root -g root -m 0755 "$release/src/locales"
for asset in index.html styles.css src/app.mjs src/api-client.mjs \
             src/sac-connect-flow.mjs src/i18n.mjs src/domain-text.mjs \
             src/locales/pl.mjs src/locales/en.mjs; do
  install -o root -g root -m 0644 "$repo/webgui/$asset" "$release/$asset"
done
install -o root -g root -m 0644 "$repo/deploy/webgui/static_server.py" "$base/.next-kiosk-server-$$"
mv -f "$base/.next-kiosk-server-$$" "$base/static_server.py"
install -d -o root -g root -m 0755 "$unit_dir"
cat > "$unit_conf" <<'CREDENTIAL'
[Service]
LoadCredential=ecu_api_token:/etc/ecu-platform-v2/api/token
CREDENTIAL
chown root:root "$unit_conf"
chmod 0644 "$unit_conf"
ln -s "releases/$revision" "$base/.next-kiosk-link-$$"
mv -Tf "$base/.next-kiosk-link-$$" "$base/current"
systemctl daemon-reload
systemctl restart ecu-webgui-static.service ecu-kiosk.service

for svc in ecu-webgui-static ecu-kiosk ecu-api-v1 ecu-sac-connect-v1 ecu-platform-v2-bench-agent; do
  systemctl is-active --quiet "$svc" || exit 2
done
for asset in / /src/app.mjs /src/api-client.mjs /src/sac-connect-flow.mjs; do
  [[ "$(curl -s --max-time 4 -o /dev/null -w '%{http_code}' "http://127.0.0.1:8877$asset")" == 200 ]] || exit 2
done
[[ "$(curl -s --max-time 4 -o /dev/null -w '%{http_code}' -H 'X-ECU-Kiosk: v1' http://127.0.0.1:8877/kiosk/v1/about)" == 200 ]] || exit 2
[[ "$(curl -s --max-time 4 -o /dev/null -w '%{http_code}' -X POST -H 'X-ECU-Kiosk: v1' -H 'Origin: http://example.invalid' http://127.0.0.1:8877/kiosk/v1/bench/daf-sac/connect)" == 403 ]] || exit 2
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' http://127.0.0.1:8878/api/v1/about)" == 401 ]] || exit 2
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:8879/api/v1/bench/daf-sac/connect)" == 401 ]] || exit 2
[[ "$(sha256sum "$dtc_file" | cut -d' ' -f1)" == "$dtc_sha" ]] || exit 2
ip -details link show can0 | grep -q 'state DOWN'
armed=0
trap - EXIT
echo "SAC_KIOSK_DEPLOY=PASS release=$revision"
echo "SAC_KIOSK_API=PASS same-origin-internal-credential"
echo "SAC_KIOSK_DTC=UNCHANGED"
echo "SAC_KIOSK_CAN=UNCHANGED_DOWN"
echo "SAC_KIOSK_ROLLBACK=sudo /usr/local/sbin/ecu-sac-kiosk-rollback"
