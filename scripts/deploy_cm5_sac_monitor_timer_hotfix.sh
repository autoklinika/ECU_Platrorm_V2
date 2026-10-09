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
# A previous smoke failure may leave an UNLINKED, root-owned staged release.
# Allow a safe retry only when all existing assets match the exact candidate
# and original deployment, never follow symlinks or reuse a foreign release.
staged=0
if [[ -e "$release" || -L "$release" ]]; then
  [[ -d "$release" && ! -L "$release" &&
     "$(stat -c '%a:%U:%G' "$release")" == 755:root:root ]] || {
    echo "SAC_TIMER_PREFLIGHT=FAIL unsafe-existing-release" >&2
    exit 2
  }
  for item in index.html styles.css src/api-client.mjs; do
    cmp -s "$base/$old_release/$item" "$release/$item" || {
      echo "SAC_TIMER_PREFLIGHT=FAIL existing-release-mismatch=$item" >&2
      exit 2
    }
  done
  for asset in src/app.mjs src/sac-parameter-monitor.mjs src/locales/pl.mjs src/locales/en.mjs; do
    cmp -s "$repo/webgui/$asset" "$release/$asset" || {
      echo "SAC_TIMER_PREFLIGHT=FAIL staged-asset-mismatch=$asset" >&2
      exit 2
    }
  done
  staged=1
fi
[[ "$(sha256sum "$base/$old_release/$monitor_rel" | cut -d' ' -f1)" == "$old_monitor_sha" ]] || exit 2
for asset in src/app.mjs src/sac-parameter-monitor.mjs src/locales/pl.mjs src/locales/en.mjs; do
  [[ -f "$repo/webgui/$asset" && ! -L "$repo/webgui/$asset" ]] || exit 2
done
[[ -f "$dtc" && ! -L "$dtc" && -f "$adapter" && ! -L "$adapter" ]] || exit 2
for svc in ecu-kiosk ecu-webgui-static ecu-sac-connect-v1 ecu-api-v1 ecu-platform-v2-bench-agent; do
  systemctl is-active --quiet "$svc" || exit 2
done
# The currently open Parameters screen can be in the middle of a bounded
# read-only cycle. Accept the first idle instant; stop the kiosk before any
# actual file or release change below.
preflight_can_down=0
for i in $(seq 1 60); do
  if ip -details link show can0 | grep -q 'state DOWN'; then
    preflight_can_down=1
    break
  fi
  sleep 0.2
done
if ((preflight_can_down != 1)); then
  echo "SAC_TIMER_PREFLIGHT=FAIL can0-never-idle" >&2
  exit 2
fi

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
# The last in-flight ECU request may still be completing CAN cleanup.
# Require a bounded idle window before confirming rollback.
rollback_down=0
for i in $(seq 1 110); do
  if ip -details link show can0 | grep -q 'state DOWN'; then
    rollback_down=$((rollback_down+1))
    if ((rollback_down >= 4)); then break; fi
  else
    rollback_down=0
  fi
  sleep 0.2
done
if ((rollback_down < 4)); then
  echo "SAC_TIMER_ROLLBACK_CAN=FAIL" >&2
  exit 2
fi
echo "SAC_TIMER_ROLLBACK=PASS"
RECOVER
chmod 0700 "$backup/rollback.sh"
printf '#!/usr/bin/env bash\nexec /usr/bin/bash %q\n' "$backup/rollback.sh" > /usr/local/sbin/ecu-sac-monitor-timer-rollback
chown root:root /usr/local/sbin/ecu-sac-monitor-timer-rollback
chmod 0700 /usr/local/sbin/ecu-sac-monitor-timer-rollback
armed=1

phase=quiesce-kiosk
# The Parameters screen may still be streaming native FE96/FEAE cycles.
# Stop the only GUI client FIRST, then permit the in-flight native probe to
# finish and release CAN. Never switch releases while measurements continue.
if ! systemctl stop ecu-kiosk.service; then
  if systemctl is-active --quiet ecu-kiosk.service; then
    echo "SAC_TIMER_QUIESCE=FAIL kiosk-remained-active" >&2
    exit 2
  fi
fi
stable_down=0
for i in $(seq 1 110); do
  if ip -details link show can0 | grep -q 'state DOWN'; then
    stable_down=$((stable_down+1))
    if ((stable_down >= 4)); then break; fi
  else
    stable_down=0
  fi
  sleep 0.2
done
if ((stable_down < 4)); then
  echo "SAC_TIMER_QUIESCE=FAIL can0-not-idle-after-kiosk-stop" >&2
  exit 2
fi
echo "SAC_TIMER_QUIESCE=PASS kiosk-stopped-can0-idle"

phase=cutover
if ((staged == 0)); then
  cp -a "$base/$old_release" "$release"
fi
for asset in src/app.mjs src/sac-parameter-monitor.mjs src/locales/pl.mjs src/locales/en.mjs; do
  install -o root -g root -m 0644 "$repo/webgui/$asset" "$release/$asset"
done
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
[[ "$ready" == 1 ]] || { echo "SAC_TIMER_SMOKE=FAIL kiosk-http-not-ready" >&2; exit 2; }
echo "SAC_TIMER_SMOKE_HTTP=PASS"
[[ "$(readlink "$base/current")" == "releases/$revision" ]] || { echo "SAC_TIMER_SMOKE=FAIL release-link" >&2; exit 2; }
for asset in src/app.mjs src/sac-parameter-monitor.mjs src/locales/pl.mjs src/locales/en.mjs; do
  cmp -s "$base/current/$asset" "$repo/webgui/$asset" || {
    echo "SAC_TIMER_SMOKE=FAIL asset-digest=$asset" >&2
    exit 2
  }
done
[[ "$(sha256sum "$dtc" | cut -d' ' -f1)" == "$dtc_before" ]] || { echo "SAC_TIMER_SMOKE=FAIL dtc-changed" >&2; exit 2; }
[[ "$(sha256sum "$adapter" | cut -d' ' -f1)" == "$adapter_before" ]] || { echo "SAC_TIMER_SMOKE=FAIL adapter-changed" >&2; exit 2; }
[[ "$(systemctl show ecu-api-v1.service -p MainPID --value)" == "$api_pid" ]] || { echo "SAC_TIMER_SMOKE=FAIL api-restarted" >&2; exit 2; }
[[ "$(systemctl show ecu-sac-connect-v1.service -p MainPID --value)" == "$sac_pid" ]] || { echo "SAC_TIMER_SMOKE=FAIL sac-adapter-restarted" >&2; exit 2; }
for svc in ecu-kiosk ecu-webgui-static ecu-sac-connect-v1 ecu-api-v1 ecu-platform-v2-bench-agent; do
  systemctl is-active --quiet "$svc" || { echo "SAC_TIMER_SMOKE=FAIL inactive-service=$svc" >&2; exit 2; }
done
# Do not sample the bus state at an arbitrary instant. An operator may be
# looking at the Parameters screen while this script runs. Require stable
# DOWN over four samples and fail with a specific reason if not quiescent.
stable_down=0
for i in $(seq 1 110); do
  if ip -details link show can0 | grep -q 'state DOWN'; then
    stable_down=$((stable_down+1))
    if ((stable_down >= 4)); then break; fi
  else
    stable_down=0
  fi
  sleep 0.2
done
if ((stable_down < 4)); then
  echo "SAC_TIMER_SMOKE=FAIL can0-not-idle" >&2
  exit 2
fi
echo "SAC_TIMER_SMOKE_CAN=PASS"
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
