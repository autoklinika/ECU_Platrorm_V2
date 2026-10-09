#!/usr/bin/env bash
# ECU V2 WebGUI — full, single-commit, atomic static release.
# Operator only. Neither installation nor tests transmit to the DUT.
set -Eeuo pipefail
umask 077
repo=/home/ecu/ECU_V2_INTEGRATION
base=/opt/ecu-platform/webgui
old_release=releases/008b673f3d8b
dtc=/var/lib/ecu-platform-v2/api-readouts/dtc-latest.v1
adapter=/usr/local/libexec/ecu-platform-v2/sac_identify_server.py
probe=/usr/local/libexec/ecu-platform-v2/sac-parameters-500k-probe
phase=preflight
backup=""
armed=0

on_failure() {
  local rc=$?
  trap - EXIT
  if ((rc != 0)); then
    echo "ECU_WEBGUI_ATOMIC=FAIL phase=$phase exit=$rc" >&2
    if ((armed == 1)); then
      bash "$backup/rollback.sh" ||
        echo "ECU_WEBGUI_ATOMIC_ROLLBACK=FAILED" >&2
    fi
  fi
}
trap on_failure EXIT

[[ "$EUID" == 0 && -t 0 && "${SUDO_USER:-}" == ecu ]] || {
  echo "ECU_WEBGUI_ATOMIC=DENIED interactive-ecu-sudo-required" >&2
  exit 77
}
[[ "$(hostname -s)" == ecu ]] || exit 2
[[ "$(runuser -u ecu -- git -C "$repo" branch --show-current)" == integration/ecu-v2-operational-candidate-20261009 ]] || exit 2
[[ -z "$(runuser -u ecu -- git -C "$repo" status --porcelain)" ]] || exit 2
revision="$(runuser -u ecu -- git -C "$repo" rev-parse --short=12 HEAD)"
release="$base/releases/$revision"
[[ -L "$base/current" && "$(readlink "$base/current")" == "$old_release" ]] || {
  echo "ECU_WEBGUI_PREFLIGHT=FAIL unexpected-deployed-release" >&2
  exit 2
}
[[ -d "$base/$old_release" && ! -L "$base/$old_release" ]] || exit 2
[[ ! -e "$release" && ! -L "$release" ]] || {
  echo "ECU_WEBGUI_PREFLIGHT=FAIL candidate-release-already-exists" >&2
  exit 2
}
for svc in ecu-kiosk ecu-webgui-static ecu-sac-connect-v1 ecu-api-v1 ecu-platform-v2-bench-agent; do
  systemctl is-active --quiet "$svc" || exit 2
done
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' -H 'X-ECU-Kiosk: v1' http://127.0.0.1:8877/kiosk/v1/about)" == 200 ]] || exit 2

# EXACT complete static-server allowlist (duplicate '/' is the same index).
# Fail when a new route is added without making this an explicit release change.
assets=(index.html styles.css src/app.mjs src/api-client.mjs
        src/sac-connect-flow.mjs src/sac-parameter-monitor.mjs
        src/i18n.mjs src/domain-text.mjs src/locales/en.mjs src/locales/pl.mjs)
export ECU_WEBGUI_EXPECTED_ASSETS="$(printf '%s\n' "${assets[@]}")"
python3 -I - <<'PY'
import os, runpy
server=runpy.run_path('/opt/ecu-platform/webgui/static_server.py')
expected=set(os.environ['ECU_WEBGUI_EXPECTED_ASSETS'].splitlines())
assert set(server['FILES'].values()) == expected, "static server allowlist mismatch"
assert len(expected) == 10
print("ECU_WEBGUI_ALLOWLIST=PASS assets=10")
PY
for asset in "${assets[@]}"; do
  [[ -f "$repo/webgui/$asset" && ! -L "$repo/webgui/$asset" ]] || exit 2
done
phase=offline-tests
runuser -u ecu -- node --test "$repo"/webgui/tests/*.test.mjs >/dev/null
browser="$(runuser -u ecu -- node "$repo/tests/issue29_browser_smoke.cjs")"
[[ "$browser" == *'REAL_CHROMIUM_GUI_MOCKED_E2E=PASS'* ]] || exit 2
runuser -u ecu -- python3 -m unittest discover -s "$repo/tests" -p test_sac_timer_deploy.py -q
dtc_sha="$(sha256sum "$dtc" | cut -d' ' -f1)"
adapter_sha="$(sha256sum "$adapter" | cut -d' ' -f1)"
probe_sha="$(sha256sum "$probe" | cut -d' ' -f1)"
api_pid="$(systemctl show ecu-api-v1 -p MainPID --value)"
adapter_pid="$(systemctl show ecu-sac-connect-v1 -p MainPID --value)"

phase=backup
install -d -o root -g root -m 0700 /var/backups/ecu-platform-v2-webgui-atomic
backup="$(mktemp -d /var/backups/ecu-platform-v2-webgui-atomic/pre-XXXXXXXX)"
printf '%s\n' "$old_release" > "$backup/previous-release"
cat > "$backup/rollback.sh" <<'RECOVER'
#!/usr/bin/env bash
set -Eeuo pipefail
dir="$(dirname "$(readlink -f "$0")")"
base=/opt/ecu-platform/webgui
prev="$(cat "$dir/previous-release")"
[[ "$prev" == releases/* && -d "$base/$prev" ]] || exit 2
ln -s "$prev" "$base/.recover-atomic-$$"
mv -Tf "$base/.recover-atomic-$$" "$base/current"
systemctl restart ecu-kiosk.service
systemctl is-active --quiet ecu-kiosk.service
echo "ECU_WEBGUI_ATOMIC_ROLLBACK=PASS"
RECOVER
chmod 0700 "$backup/rollback.sh"
printf '#!/usr/bin/env bash\nexec /usr/bin/bash %q\n' "$backup/rollback.sh" > /usr/local/sbin/ecu-webgui-atomic-rollback
chown root:root /usr/local/sbin/ecu-webgui-atomic-rollback
chmod 0700 /usr/local/sbin/ecu-webgui-atomic-rollback
armed=1

phase=quiesce
# Only the kiosk is the GUI caller. Stop it and allow the last native read to
# finish before changing the symlink. No probe/ECU operation is initiated here.
systemctl stop ecu-kiosk.service || true
systemctl is-active --quiet ecu-kiosk.service && {
  echo "ECU_WEBGUI_QUIESCE=FAIL kiosk-still-active" >&2
  exit 2
}
down=0
for i in $(seq 1 120); do
  if ip -details link show can0 | grep -q 'state DOWN'; then
    down=$((down+1))
    if ((down >= 4)); then break; fi
  else down=0; fi
  sleep 0.2
done
((down >= 4)) || { echo "ECU_WEBGUI_QUIESCE=FAIL can0-not-down" >&2; exit 2; }
echo "ECU_WEBGUI_QUIESCE=PASS"

phase=stage
install -d -o root -g root -m 0755 "$release/src/locales"
for asset in "${assets[@]}"; do
  install -o root -g root -m 0644 "$repo/webgui/$asset" "$release/$asset"
  cmp -s "$repo/webgui/$asset" "$release/$asset" || {
    echo "ECU_WEBGUI_STAGE=FAIL asset=$asset" >&2
    exit 2
  }
done
printf 'commit=%s\n' "$(runuser -u ecu -- git -C "$repo" rev-parse HEAD)" > "$release/manifest.txt"
for asset in "${assets[@]}"; do
  digest="$(sha256sum "$release/$asset" | cut -d' ' -f1)"
  printf '%s  %s\n' "$digest" "$asset" >> "$release/manifest.txt"
done
chmod 0644 "$release/manifest.txt"
echo "ECU_WEBGUI_STAGE=PASS complete-allowlist=10"

phase=prestart-smoke
ln -s "releases/$revision" "$base/.new-atomic-$$"
mv -Tf "$base/.new-atomic-$$" "$base/current"
for asset in "${assets[@]}"; do
  cmp -s "$base/current/$asset" "$repo/webgui/$asset" || exit 2
done
(cd "$base/current" && tail -n +2 manifest.txt | sha256sum --check --status) || {
  echo "ECU_WEBGUI_SMOKE=FAIL sha256-manifest" >&2
  exit 2
}
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' http://127.0.0.1:8877/src/app.mjs)" == 200 ]] || exit 2

phase=start-kiosk
systemctl start ecu-kiosk.service
for i in $(seq 1 45); do
  if systemctl is-active --quiet ecu-kiosk.service; then break; fi
  sleep 0.2
done
systemctl is-active --quiet ecu-kiosk.service || exit 2
for svc in ecu-webgui-static ecu-sac-connect-v1 ecu-api-v1 ecu-platform-v2-bench-agent; do
  systemctl is-active --quiet "$svc" || exit 2
done
[[ "$(sha256sum "$dtc" | cut -d' ' -f1)" == "$dtc_sha" ]] || exit 2
[[ "$(sha256sum "$adapter" | cut -d' ' -f1)" == "$adapter_sha" ]] || exit 2
[[ "$(sha256sum "$probe" | cut -d' ' -f1)" == "$probe_sha" ]] || exit 2
[[ "$(systemctl show ecu-api-v1 -p MainPID --value)" == "$api_pid" ]] || exit 2
[[ "$(systemctl show ecu-sac-connect-v1 -p MainPID --value)" == "$adapter_pid" ]] || exit 2
armed=0
trap - EXIT
echo "ECU_WEBGUI_ATOMIC=PASS commit=$revision assets=10"
echo "ECU_WEBGUI_DTC=UNCHANGED"
echo "ECU_WEBGUI_BACKEND=UNCHANGED"
echo "ECU_WEBGUI_ROLLBACK=sudo /usr/local/sbin/ecu-webgui-atomic-rollback"
