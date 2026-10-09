#!/usr/bin/env bash
# Issue #29: guarded deployment of a previously built, read-only SAC candidate.
# Usage (local CM5 terminal): sudo bash scripts/deploy_cm5_sac_issue29.sh --physical
# Installation is read-only; --physical explicitly allows two UDS/FEAE reads.
# No main merge, no DTC modification or ECU actuation.
set -Eeuo pipefail
umask 077
repo=/home/ecu/ECU_V2_INTEGRATION
build="$repo/build/issue29-release"
manifest="$build/issue29-deploy.manifest"
base=/opt/ecu-platform/webgui
adapter=/usr/local/libexec/ecu-platform-v2/sac_identify_server.py
native=/usr/local/libexec/ecu-platform-v2/sac-parameters-500k-probe
candidate="$build/tests/ecu_daf_sac_stage42_read_probe"
dtc=/var/lib/ecu-platform-v2/api-readouts/dtc-latest.v1
old_release=releases/81a390641a18
backup=""
armed=0
phase=preflight
[[ "$#" == 1 && "$1" == --physical ]] || {
  echo "Usage: sudo bash scripts/deploy_cm5_sac_issue29.sh --physical" >&2
  exit 64
}

recover_on_error() {
  local code=$?
  trap - EXIT
  if ((code != 0)); then
    echo "SAC_ISSUE29_DEPLOY=FAIL phase=$phase exit=$code" >&2
    if ((armed == 1)); then
      bash "$backup/rollback.sh" ||
        echo "SAC_ISSUE29_ROLLBACK=FAILED manual-recovery-required" >&2
    fi
  fi
}
trap recover_on_error EXIT

[[ "$EUID" == 0 && -t 0 && "${SUDO_USER:-}" == ecu ]] || {
  echo "SAC_ISSUE29_DEPLOY=DENIED requires interactive operator sudo" >&2
  exit 77
}
[[ "$(hostname -s)" == ecu ]] || exit 2
[[ -f "$manifest" && ! -L "$manifest" && -x "$candidate" && ! -L "$candidate" ]] || exit 2
read -r expected_rev expected_sha < "$manifest"
[[ "$expected_rev" =~ ^[0-9a-f]{40}$ && "$expected_sha" =~ ^[0-9a-f]{64}$ ]] || exit 2
[[ "$(runuser -u ecu -- git -C "$repo" rev-parse HEAD)" == "$expected_rev" ]] || exit 2
[[ "$(runuser -u ecu -- git -C "$repo" branch --show-current)" == integration/ecu-v2-operational-candidate-20261009 ]] || exit 2
[[ -z "$(runuser -u ecu -- git -C "$repo" status --porcelain)" ]] || exit 2
[[ "$(sha256sum "$candidate" | cut -d' ' -f1)" == "$expected_sha" ]] || exit 2
strings "$candidate" | grep -Fx 'SAC_API_PARAMETERS_CAPTURED_AT_UNIX_MS=' >/dev/null || exit 2
for item in "$adapter" "$native"; do
  [[ -f "$item" && ! -L "$item" &&
     "$(stat -c '%a:%U:%G' "$item")" == 755:root:root ]] || exit 2
done
[[ -L "$base/current" && "$(readlink "$base/current")" == "$old_release" ]] || exit 2
[[ -f "$dtc" && ! -L "$dtc" &&
   "$(stat -c '%a:%U:%G' "$dtc")" == 640:ecu:ecu-api-read ]] || exit 2
[[ -f /etc/systemd/system/ecu-webgui-static.service.d/50-ecu-prototype-credential.conf ]] || exit 2
[[ "$(systemctl show -p DynamicUser --value ecu-webgui-static.service)" == yes ]] || exit 2
for svc in ecu-api-v1 ecu-sac-connect-v1 ecu-webgui-static ecu-kiosk ecu-platform-v2-bench-agent; do
  systemctl is-active --quiet "$svc" || exit 2
done
ip -details link show can0 | grep -q 'state DOWN' || exit 2
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' -H 'X-ECU-Kiosk: v1' http://127.0.0.1:8877/kiosk/v1/about)" == 200 ]] || exit 2
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:8879/api/v1/bench/daf-sac/connect)" == 401 ]] || exit 2
release="$base/releases/$(runuser -u ecu -- git -C "$repo" rev-parse --short=12 HEAD)"
[[ ! -e "$release" && ! -L "$release" ]] || exit 2

phase=offline-regression
runuser -u ecu -- python3 -m unittest discover -s "$repo/tests" -p test_sac_connect_adapter.py -q
runuser -u ecu -- python3 -m unittest discover -s "$repo/tests" -p test_issue29_capture_contract.py -q
runuser -u ecu -- node --test "$repo"/webgui/tests/*.test.mjs >/dev/null
runuser -u ecu -- ctest --test-dir "$build" -R '^ecu[.]api[.]' --output-on-failure
python3 -m py_compile "$repo/deploy/sac_connect/sac_identify_server.py"
dtc_sha="$(sha256sum "$dtc" | cut -d' ' -f1)"
params=/var/lib/ecu-platform-v2/api-readouts/sac-parameters-latest.v1
params_sha="none"
[[ ! -e "$params" || -L "$params" ]] || params_sha="$(sha256sum "$params" | cut -d' ' -f1)"

phase=backup
install -d -o root -g root -m 0700 /var/backups/ecu-platform-v2-issue29
backup="$(mktemp -d /var/backups/ecu-platform-v2-issue29/pre-XXXXXXXX)"
cp -a "$adapter" "$backup/sac_identify_server.py"
cp -a "$native" "$backup/sac-parameters-500k-probe"
printf '%s\n' "$old_release" > "$backup/previous-link"
printf '%s\n' "$dtc_sha" > "$backup/dtc-before.sha"
printf '%s\n' "$params_sha" > "$backup/parameters-before.sha"
cat > "$backup/rollback.sh" <<'RECOVERY'
#!/usr/bin/env bash
set -Eeuo pipefail
root="$(dirname "$(readlink -f "$0")")"
base=/opt/ecu-platform/webgui
lib=/usr/local/libexec/ecu-platform-v2
old="$(cat "$root/previous-link")"
[[ "$old" == releases/* && -d "$base/$old" ]] || exit 2
for asset in sac_identify_server.py sac-parameters-500k-probe; do
  [[ -f "$root/$asset" && ! -L "$root/$asset" ]] || exit 2
  install -o root -g root -m 0755 "$root/$asset" "$lib/.restore-$asset-$$"
  mv -f "$lib/.restore-$asset-$$" "$lib/$asset"
done
ln -s "$old" "$base/.restore-issue29-$$"
mv -Tf "$base/.restore-issue29-$$" "$base/current"
systemctl restart ecu-sac-connect-v1.service ecu-webgui-static.service ecu-kiosk.service
for svc in ecu-sac-connect-v1 ecu-api-v1 ecu-webgui-static ecu-kiosk ecu-platform-v2-bench-agent; do
  systemctl is-active --quiet "$svc"
done
ip -details link show can0 | grep -q 'state DOWN'
echo "SAC_ISSUE29_ROLLBACK=PASS"
RECOVERY
chmod 0700 "$backup/rollback.sh"
printf '#!/usr/bin/env bash\nexec /usr/bin/bash %q\n' "$backup/rollback.sh" > /usr/local/sbin/ecu-sac-issue29-rollback
chown root:root /usr/local/sbin/ecu-sac-issue29-rollback
chmod 0700 /usr/local/sbin/ecu-sac-issue29-rollback
armed=1

phase=install
install -d -o root -g root -m 0755 "$release/src/locales"
for asset in index.html styles.css src/app.mjs src/api-client.mjs \
             src/sac-connect-flow.mjs src/i18n.mjs src/domain-text.mjs \
             src/locales/pl.mjs src/locales/en.mjs; do
  install -o root -g root -m 0644 "$repo/webgui/$asset" "$release/$asset"
done
install -o root -g root -m 0755 "$candidate" "$native.issue29-next-$$"
install -o root -g root -m 0755 "$repo/deploy/sac_connect/sac_identify_server.py" "$adapter.issue29-next-$$"
mv -f "$native.issue29-next-$$" "$native"
mv -f "$adapter.issue29-next-$$" "$adapter"
ln -s "releases/$(basename "$release")" "$base/.issue29-link-$$"
mv -Tf "$base/.issue29-link-$$" "$base/current"
systemctl restart ecu-sac-connect-v1.service ecu-webgui-static.service ecu-kiosk.service

phase=health
ready=0
for attempt in $(seq 1 40); do
  s1="$(curl -s --max-time 2 -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:8879/api/v1/bench/daf-sac/connect 2>/dev/null || true)"
  s2="$(curl -s --max-time 2 -o /dev/null -w '%{http_code}' -H 'X-ECU-Kiosk: v1' http://127.0.0.1:8877/kiosk/v1/about 2>/dev/null || true)"
  if [[ "$s1" == 401 && "$s2" == 200 ]]; then ready=1; break; fi
  sleep 0.25
done
[[ "$ready" == 1 ]] || { echo "SAC_ISSUE29_HTTP=FAIL adapter=$s1 kiosk=$s2"; exit 2; }
for svc in ecu-api-v1 ecu-sac-connect-v1 ecu-webgui-static ecu-kiosk ecu-platform-v2-bench-agent; do
  systemctl is-active --quiet "$svc" || exit 2
done
for asset in / /src/app.mjs /src/api-client.mjs /src/locales/pl.mjs; do
  [[ "$(curl -s --max-time 4 -o /dev/null -w '%{http_code}' "http://127.0.0.1:8877$asset")" == 200 ]] || exit 2
done
[[ "$(sha256sum "$native" | cut -d' ' -f1)" == "$expected_sha" ]] || exit 2
[[ "$(sha256sum "$adapter" | cut -d' ' -f1)" == "$(sha256sum "$repo/deploy/sac_connect/sac_identify_server.py" | cut -d' ' -f1)" ]] || exit 2
[[ "$(sha256sum "$dtc" | cut -d' ' -f1)" == "$dtc_sha" ]] || exit 2
if [[ "$params_sha" != none ]]; then
  [[ "$(sha256sum "$params" | cut -d' ' -f1)" == "$params_sha" ]] || exit 2
fi
ip -details link show can0 | grep -q 'state DOWN' || exit 2

phase=physical-read-only-acceptance
runuser -u ecu -- python3 -I "$repo/scripts/verify_cm5_sac_issue29_physical.py" --physical
[[ "$(sha256sum "$dtc" | cut -d' ' -f1)" == "$dtc_sha" ]] || exit 2
ip -details link show can0 | grep -q 'state DOWN' || exit 2

armed=0
trap - EXIT
echo "SAC_ISSUE29_DEPLOY=PASS commit=$expected_rev"
echo "SAC_ISSUE29_NATIVE_PROBE=PASS sha256=$expected_sha"
echo "SAC_ISSUE29_GUI=PASS release=$(basename "$release")"
echo "SAC_ISSUE29_API=UNCHANGED"
echo "SAC_ISSUE29_DTC=UNCHANGED"
echo "SAC_ISSUE29_CAN=DOWN_AFTER_TWO_READ_ONLY_PHYSICAL_CYCLES"
echo "SAC_ISSUE29_ROLLBACK=sudo /usr/local/sbin/ecu-sac-issue29-rollback"
