#!/usr/bin/env bash
# Issue #29: faster parameter refresh and honest FEAE status, WebGUI only.
# No CAN operations during deployment and no backend modification.
set -Eeuo pipefail
umask 077
repo=/home/ecu/ECU_V2_INTEGRATION
base=/opt/ecu-platform/webgui
old_release=releases/e804cdbd67e4
monitor_rel=src/sac-parameter-monitor.mjs
old_monitor_sha=e013fae25fff3339ec7c43e39bb19b356a4c9fd8e5c8cf10b9b2435c8ac7c16d
dtc=/var/lib/ecu-platform-v2/api-readouts/dtc-latest.v1
adapter=/usr/local/libexec/ecu-platform-v2/sac_identify_server.py
armed=0
phase=preflight
backup=""
fail_rollback() {
  local rc=$?
  trap - EXIT
  if ((rc != 0)); then
    echo "SAC_TIMER_HOTFIX=FAIL phase=$phase exit=$rc" >&2
    if ((armed == 1)); then
      bash "$backup/rollback.sh" ||
        echo "SAC_TIMER_ROLLBACK=FAILED operator-recovery-needed" >&2
    fi
  fi
}
trap fail_rollback EXIT

[[ "$EUID" == 0 && -t 0 && "${SUDO_USER:-}" == ecu ]] || {
  echo "SAC_TIMER_HOTFIX=DENIED interactive-ecu-sudo-required"; exit 77;
}
[[ "$(hostname -s)" == ecu ]] || exit 2
[[ "$(runuser -u ecu -- git -C "$repo" branch --show-current)" == integration/ecu-v2-operational-candidate-20261009 ]] || exit 2
[[ -z "$(runuser -u ecu -- git -C "$repo" status --porcelain)" ]] || exit 2
revision="$(runuser -u ecu -- git -C "$repo" rev-parse --short=12 HEAD)"
release="$base/releases/$revision"
[[ -L "$base/current" && "$(readlink "$base/current")" == "$old_release" ]] || exit 2
[[ -d "$base/$old_release" && ! -L "$base/$old_release" ]] || exit 2
[[ ! -e "$release" && ! -L "$release" ]] || exit 2
[[ "$(sha256sum "$base/$old_release/$monitor_rel" | cut -d' ' -f1)" == "$old_monitor_sha" ]] || exit 2
[[ -f "$repo/webgui/$monitor_rel" && ! -L "$repo/webgui/$monitor_rel" ]] || exit 2
[[ -f "$dtc" && ! -L "$dtc" && -f "$adapter" && ! -L "$adapter" ]] || exit 2
for svc in ecu-kiosk ecu-webgui-static ecu-sac-connect-v1 ecu-api-v1 ecu-platform-v2-bench-agent; do
  systemctl is-active --quiet "$svc" || exit 2
done
ip -details link show can0 | grep -q 'state DOWN' || exit 2

phase=offline-tests
runuser -u ecu -- node --test "$repo"/webgui/tests/*.test.mjs >/dev/null
browser_proof="$(runuser -u ecu -- node "$repo/tests/issue29_browser_smoke.cjs")"
[[ "$browser_proof" == *'REAL_CHROMIUM_GUI_MOCKED_E2E=PASS'* ]] || exit 2
dtc_before="$(sha256sum "$dtc" | cut -d' ' -f1)"
adapter_before="$(sha256sum "$adapter" | cut -d' ' -f1)"
api_pid="$(systemctl show ecu-api-v1.service -p MainPID --value)"
sac_pid="$(systemctl show ecu-sac-connect-v1.service -p MainPID --value)"

phase=backup
install -d -o root -g root -m 0700 /var/backups/ecu-platform-v2-sac-timer
backup="$(mktemp -d /var/backups/ecu-platform-v2-sac-timer/pre-XXXXXXXX)"
printf '%s\n' "$old_release" > "$backup/previous-release"
cat > "$backup/rollback.sh" <<'RECOVER'
#!/usr/bin/env bash
set -Eeuo pipefail
dir="$(dirname "$(readlink -f "$0")")"
base=/opt/ecu-platform/webgui
old="$(cat "$dir/previous-release")"
[[ "$old" == releases/* && -d "$base/$old" ]] || exit 2
ln -s "$old" "$base/.rollback-timer-$$"
mv -Tf "$base/.rollback-timer-$$" "$base/current"
systemctl restart ecu-kiosk.service
systemctl is-active --quiet ecu-kiosk.service
ip -details link show can0 | grep -q 'state DOWN'
echo "SAC_TIMER_ROLLBACK=PASS"
RECOVER
chmod 0700 "$backup/rollback.sh"
printf '#!/usr/bin/env bash\nexec /usr/bin/bash %q\n' "$backup/rollback.sh" > /usr/local/sbin/ecu-sac-monitor-timer-rollback
chown root:root /usr/local/sbin/ecu-sac-monitor-timer-rollback
chmod 0700 /usr/local/sbin/ecu-sac-monitor-timer-rollback
armed=1

phase=cutover
cp -a "$base/$old_release" "$release"
install -o root -g root -m 0644 "$repo/webgui/$monitor_rel" "$release/$monitor_rel"
ln -s "releases/$revision" "$base/.timer-hotfix-$$"
mv -Tf "$base/.timer-hotfix-$$" "$base/current"
systemctl restart ecu-kiosk.service

phase=smoke
ready=0
for i in $(seq 1 30); do
  if systemctl is-active --quiet ecu-kiosk.service &&
     [[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' http://127.0.0.1:8877/src/sac-parameter-monitor.mjs 2>/dev/null || true)" == 200 ]]; then
    ready=1
    break
  fi
  sleep 0.25
done
[[ "$ready" == 1 ]] || exit 2
[[ "$(readlink "$base/current")" == "releases/$revision" ]] || exit 2
[[ "$(sha256sum "$base/current/$monitor_rel" | cut -d' ' -f1)" == "$(sha256sum "$repo/webgui/$monitor_rel" | cut -d' ' -f1)" ]] || exit 2
[[ "$(sha256sum "$dtc" | cut -d' ' -f1)" == "$dtc_before" ]] || exit 2
[[ "$(sha256sum "$adapter" | cut -d' ' -f1)" == "$adapter_before" ]] || exit 2
[[ "$(systemctl show ecu-api-v1.service -p MainPID --value)" == "$api_pid" ]] || exit 2
[[ "$(systemctl show ecu-sac-connect-v1.service -p MainPID --value)" == "$sac_pid" ]] || exit 2
for svc in ecu-kiosk ecu-webgui-static ecu-sac-connect-v1 ecu-api-v1 ecu-platform-v2-bench-agent; do
  systemctl is-active --quiet "$svc" || exit 2
done
ip -details link show can0 | grep -q 'state DOWN' || exit 2
armed=0
trap - EXIT
echo "SAC_TIMER_HOTFIX=PASS release=$revision"
echo "SAC_PARAMETER_REFRESH=200MS_POST_COMPLETION_NO_OVERLAP"
echo "SAC_FEAE_UNAVAILABLE=EXPLICIT_NOT_ZERO"
echo "SAC_TIMER_BROWSER=PASS real-chromium-mocked-4-parameter-cycles"
echo "SAC_TIMER_BACKEND=UNCHANGED"
echo "SAC_TIMER_DTC=UNCHANGED"
echo "SAC_TIMER_CAN=DOWN_NO_ECU_PROBE_DURING_DEPLOY"
echo "SAC_TIMER_ROLLBACK=sudo /usr/local/sbin/ecu-sac-monitor-timer-rollback"
