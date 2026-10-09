#!/usr/bin/env bash
# Operator-controlled two-speed SAC read-only update; no physical probe during installation.
set -Eeuo pipefail
umask 077
repo=/home/ecu/ECU_WEBGUI_PARAMS_V1
base=/opt/ecu-platform/webgui
adapter=/usr/local/libexec/ecu-platform-v2/sac_identify_server.py
expected_adapter=5e523611979fecf6468eb02c62b19304f408fb6df0f8451834e13848a2e2a8e7
expected_gui=releases/e62d4d1d4d93
backup=""
stage="preflight"
armed=0
rollback_on_error() {
  local rc=$?
  trap - EXIT
  if ((rc != 0)); then
    echo "SAC_DUAL_BITRATE_DEPLOY=FAIL stage=$stage exit=$rc" >&2
    if ((armed == 1)); then
      bash "$backup/rollback.sh" ||
        echo "SAC_DUAL_BITRATE_ROLLBACK=FAILED manual-recovery-required" >&2
    fi
  fi
}
trap rollback_on_error EXIT

[[ "$EUID" == 0 && -t 0 && "${SUDO_USER:-}" == ecu ]] || {
  echo SAC_DUAL_BITRATE_DEPLOY=DENIED interactive-ecu-sudo-required; exit 77;
}
[[ "$(hostname -s)" == ecu ]] || exit 2
[[ "$(runuser -u ecu -- git -C "$repo" branch --show-current)" == webgui/sac-local-prototype-no-token-20261009 ]] || exit 2
[[ -z "$(runuser -u ecu -- git -C "$repo" status --porcelain)" ]] || exit 2
[[ -L "$base/current" && "$(readlink "$base/current")" == "$expected_gui" ]] || exit 2
[[ -f "$adapter" && ! -L "$adapter" && "$(stat -c '%U:%G' "$adapter")" == root:root ]] || exit 2
[[ "$(sha256sum "$adapter" | cut -d' ' -f1)" == "$expected_adapter" ]] || exit 2
[[ -f /etc/systemd/system/ecu-webgui-static.service.d/50-ecu-prototype-credential.conf ]] || exit 2
[[ "$(systemctl show -p DynamicUser --value ecu-webgui-static.service)" == yes ]] || exit 2
for unit in ecu-sac-connect-v1 ecu-api-v1 ecu-platform-v2-bench-agent ecu-webgui-static ecu-kiosk; do
  systemctl is-active --quiet "$unit" || exit 2
done
ip -details link show can0 | grep -q 'state DOWN' || exit 2
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' -H 'X-ECU-Kiosk: v1' http://127.0.0.1:8877/kiosk/v1/about)" == 200 ]] || exit 2
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' http://127.0.0.1:8878/api/v1/about)" == 401 ]] || exit 2
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:8879/api/v1/bench/daf-sac/connect)" == 401 ]] || exit 2

stage="local-regression"
runuser -u ecu -- python3 -m unittest discover -s "$repo/tests" -p test_sac_connect_adapter.py -q
runuser -u ecu -- node --test "$repo"/webgui/tests/*.test.mjs >/dev/null
python3 -m py_compile "$repo/deploy/sac_connect/sac_identify_server.py"
node --check "$repo/webgui/src/api-client.mjs"
node --check "$repo/webgui/src/app.mjs"

stage="snapshot"
dtc_file=/var/lib/ecu-platform-v2/api-readouts/dtc-latest.v1
[[ -f "$dtc_file" ]] || exit 2
dtc_sha="$(sha256sum "$dtc_file" | cut -d' ' -f1)"
old="$expected_gui"
revision="$(runuser -u ecu -- git -C "$repo" rev-parse --short=12 HEAD)"
release="$base/releases/$revision"
[[ ! -e "$release" && ! -L "$release" ]] || exit 2
install -d -o root -g root -m 0700 /var/backups/ecu-platform-v2-sac-dual-rate
backup="$(mktemp -d /var/backups/ecu-platform-v2-sac-dual-rate/pre-XXXXXXXX)"
install -o root -g root -m 0700 "$adapter" "$backup/original.py"
printf '%s\n' "$old" > "$backup/previous-link"
cat > "$backup/rollback.sh" <<'RECOVERY'
#!/usr/bin/env bash
set -Eeuo pipefail
base=/opt/ecu-platform/webgui
dir="$(dirname "$(readlink -f "$0")")"
old="$(cat "$dir/previous-link")"
[[ "$old" == releases/* && -d "$base/$old" ]] || exit 2
install -o root -g root -m 0755 "$dir/original.py" "/usr/local/libexec/ecu-platform-v2/.restore-sac-$$"
mv -f "/usr/local/libexec/ecu-platform-v2/.restore-sac-$$" /usr/local/libexec/ecu-platform-v2/sac_identify_server.py
ln -s "$old" "$base/.restore-sac-link-$$"
mv -Tf "$base/.restore-sac-link-$$" "$base/current"
systemctl restart ecu-sac-connect-v1.service ecu-webgui-static.service ecu-kiosk.service
for service in ecu-sac-connect-v1 ecu-webgui-static ecu-kiosk; do
  systemctl is-active --quiet "$service"
done
ip -details link show can0 | grep -q 'state DOWN'
echo SAC_DUAL_BITRATE_ROLLBACK=PASS
RECOVERY
chmod 0700 "$backup/rollback.sh"
printf '#!/usr/bin/env bash\nexec /usr/bin/bash %q\n' "$backup/rollback.sh"   > /usr/local/sbin/ecu-sac-dual-bitrate-rollback
chmod 0700 /usr/local/sbin/ecu-sac-dual-bitrate-rollback
armed=1

stage="install"
install -d -o root -g root -m 0755 "$release/src/locales"
for asset in index.html styles.css src/app.mjs src/api-client.mjs \
             src/sac-connect-flow.mjs src/i18n.mjs src/domain-text.mjs \
             src/locales/en.mjs src/locales/pl.mjs; do
  install -o root -g root -m 0644 "$repo/webgui/$asset" "$release/$asset"
done
install -o root -g root -m 0755 "$repo/deploy/sac_connect/sac_identify_server.py" "$adapter.next-$$"
mv -f "$adapter.next-$$" "$adapter"
ln -s "releases/$revision" "$base/.dual-rate-$$"
mv -Tf "$base/.dual-rate-$$" "$base/current"
systemctl restart ecu-sac-connect-v1.service ecu-webgui-static.service ecu-kiosk.service

stage="http-readiness"
ready=0
for attempt in $(seq 1 30); do
  s1="$(curl -s --max-time 2 -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:8879/api/v1/bench/daf-sac/connect 2>/dev/null || true)"
  s2="$(curl -s --max-time 2 -o /dev/null -w '%{http_code}' -H 'X-ECU-Kiosk: v1' http://127.0.0.1:8877/kiosk/v1/about 2>/dev/null || true)"
  if [[ "$s1" == 401 && "$s2" == 200 ]]; then ready=1; break; fi
  sleep 0.25
done
[[ "$ready" == 1 ]] || { echo "SAC_DUAL_BITRATE_HTTP=FAIL adapter=$s1 kiosk=$s2"; exit 2; }
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' http://127.0.0.1:8877/src/app.mjs)" == 200 ]] || exit 2
[[ "$(sha256sum "$adapter" | cut -d' ' -f1)" == "$(sha256sum "$repo/deploy/sac_connect/sac_identify_server.py" | cut -d' ' -f1)" ]] || exit 2
[[ "$(sha256sum "$dtc_file" | cut -d' ' -f1)" == "$dtc_sha" ]] || exit 2
ip -details link show can0 | grep -q 'state DOWN' || exit 2

armed=0
trap - EXIT
echo "SAC_DUAL_BITRATE_DEPLOY=PASS commit=$revision"
echo "SAC_DUAL_BITRATE_POLICY=250000_THEN_500000_UDS_ONLY"
echo "SAC_DUAL_BITRATE_DTC=UNCHANGED"
echo "SAC_DUAL_BITRATE_CAN=DOWN_NO_PROBE_DURING_INSTALL"
echo "SAC_DUAL_BITRATE_ROLLBACK=sudo /usr/local/sbin/ecu-sac-dual-bitrate-rollback"
