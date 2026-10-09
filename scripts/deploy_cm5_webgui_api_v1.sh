#!/usr/bin/env bash
# Operator-only, scoped CM5 WebGUI API V1 static release. Does not touch API/Bench/CAN.
set -Eeuo pipefail
umask 077

repo=/home/ecu/ECU_WebGUI_API_V1
base=/opt/ecu-platform/webgui
backup=""
armed=0
rollback() {
  local status="$?"
  trap - EXIT ERR
  if [[ "$status" -ne 0 ]]; then
    echo "WEBGUI_API_CLIENT_DEPLOY=FAIL code=$status" >&2
    if [[ "$armed" == 1 ]]; then
      echo "WEBGUI_API_CLIENT_ROLLBACK=START" >&2
      /usr/bin/bash /usr/local/sbin/ecu-webgui-api-v1-rollback || \
        echo "WEBGUI_API_CLIENT_ROLLBACK=FAILED manual-recovery-required" >&2
    fi
  fi
}
trap rollback EXIT

[[ "$EUID" -eq 0 && -t 0 ]] || {
  echo "WEBGUI_API_CLIENT_DEPLOY=DENIED interactive-root-terminal-required"; exit 77;
}
[[ "$(hostname -s)" == ecu ]] || exit 2
[[ "$(runuser -u ecu -- git -C "$repo" branch --show-current)" == \
   webgui/api-v1-readonly-client-20261008 ]] || exit 2
[[ -z "$(runuser -u ecu -- git -C "$repo" status --porcelain)" ]] || {
  echo "WEBGUI_API_CLIENT_DEPLOY=DIRTY_TREE"; exit 2;
}
[[ -L "$base/current" && -f "$base/static_server.py" ]] || exit 2
[[ "$(systemctl show -p User --value ecu-kiosk.service)" == ecu-kiosk ]] || exit 2
for service in ecu-kiosk.service ecu-webgui-static.service \
               ecu-api-v1.service ecu-platform-v2-bench-agent.service; do
  systemctl is-active --quiet "$service" || exit 2
done
ip -details link show can0 | grep -q 'state DOWN' || {
  echo "WEBGUI_API_CLIENT_DEPLOY=CAN_MUST_BE_DOWN"; exit 2;
}
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' \
  http://127.0.0.1:8877/)" == 200 ]] || exit 2
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' \
  http://127.0.0.1:8878/api/v1/about)" == 401 ]] || exit 2

revision="$(runuser -u ecu -- git -C "$repo" rev-parse --short=12 HEAD)"
release="$base/releases/$revision"
[[ ! -e "$release" && ! -L "$release" ]] || {
  echo "WEBGUI_API_CLIENT_DEPLOY=RELEASE_ALREADY_EXISTS"; exit 2;
}
previous="$(readlink "$base/current")"
[[ "$previous" == releases/* && -d "$base/$previous" ]] || exit 2

# Persistent recovery independent of Git, the kiosk and the source checkout.
install -d -o root -g root -m 0700 /var/backups/ecu-platform-webgui-api
backup="$(mktemp -d /var/backups/ecu-platform-webgui-api/pre-XXXXXXXX)"
printf '%s\n' "$previous" > "$backup/previous-link"
cp -a "$base/static_server.py" "$backup/static_server.py"
cat > "$backup/rollback.sh" <<'RECOVERY'
#!/usr/bin/env bash
set -Eeuo pipefail
base=/opt/ecu-platform/webgui
backup="$(dirname "$(readlink -f "$0")")"
old="$(cat "$backup/previous-link")"
[[ "$old" == releases/* && -d "$base/$old" ]] || exit 2
cp -a "$backup/static_server.py" "$base/.static-server-restore-$$"
mv -f "$base/.static-server-restore-$$" "$base/static_server.py"
ln -s "$old" "$base/.current-restore-$$"
mv -Tf "$base/.current-restore-$$" "$base/current"
systemctl restart ecu-webgui-static.service
systemctl restart ecu-kiosk.service
systemctl is-active --quiet ecu-webgui-static.service
systemctl is-active --quiet ecu-kiosk.service
echo WEBGUI_API_CLIENT_ROLLBACK=PASS
RECOVERY
chmod 0700 "$backup/rollback.sh"
# Stable root-owned recovery entrypoint for this specific deployment.
printf '#!/usr/bin/env bash\nexec /usr/bin/bash %q\n' "$backup/rollback.sh" \
  > /usr/local/sbin/ecu-webgui-api-v1-rollback
chown root:root /usr/local/sbin/ecu-webgui-api-v1-rollback
chmod 0700 /usr/local/sbin/ecu-webgui-api-v1-rollback
armed=1

install -d -o root -g root -m 0755 "$release/src/locales"
for asset in index.html styles.css src/app.mjs src/api-client.mjs \
             src/i18n.mjs src/domain-text.mjs \
             src/locales/en.mjs src/locales/pl.mjs; do
  install -o root -g root -m 0644 "$repo/webgui/$asset" "$release/$asset"
done
install -o root -g root -m 0644 "$repo/deploy/webgui/static_server.py" \
  "$base/.static-server-next-$$"
mv -f "$base/.static-server-next-$$" "$base/static_server.py"
ln -s "releases/$revision" "$base/.current-next-$$"
mv -Tf "$base/.current-next-$$" "$base/current"

systemctl restart ecu-webgui-static.service
systemctl restart ecu-kiosk.service
systemctl is-active --quiet ecu-webgui-static.service
systemctl is-active --quiet ecu-kiosk.service
systemctl is-active --quiet ecu-api-v1.service
systemctl is-active --quiet ecu-platform-v2-bench-agent.service

for asset in / /src/app.mjs /src/api-client.mjs; do
  [[ "$(curl -s --max-time 4 -o /dev/null -w '%{http_code}' \
    "http://127.0.0.1:8877$asset")" == 200 ]] || exit 2
done
curl -fsSI --max-time 4 http://127.0.0.1:8877/ | \
  grep -q "connect-src 'self' http://127.0.0.1:8878" || exit 2
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' \
  http://127.0.0.1:8878/api/v1/about)" == 401 ]] || exit 2
ip -details link show can0 | grep -q 'state DOWN' || exit 2

armed=0
trap - EXIT ERR
echo "WEBGUI_API_CLIENT_DEPLOY=PASS revision=$revision"
echo "WEBGUI_API_CLIENT_ROLLBACK=sudo /usr/local/sbin/ecu-webgui-api-v1-rollback"
echo WEBGUI_API_CLIENT_NOTES="No token provisioned to kiosk; operator input required"
