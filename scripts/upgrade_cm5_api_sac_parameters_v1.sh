#!/usr/bin/env bash
# Scoped operator-only API binary upgrade; preserves token, identity, unit
# and DTC evidence. Old binary restored on any post-install smoke failure.
set -Eeuo pipefail
umask 077
repo=/home/ecu/ECU_API_PARAMS_V1
unit=ecu-api-v1.service
destination=/usr/local/libexec/ecu-platform-v2/ecu_api_http
stamp="$repo/build/params-linux/api_parameters_candidate.sha"
candidate="$repo/build/params-linux/src/api/ecu_api_http"
armed=0
on_exit() {
  local rc=$?
  trap - EXIT ERR
  if [[ "$rc" -ne 0 ]]; then
    echo "SAC_API_UPGRADE=FAIL code=$rc" >&2
    if [[ "$armed" == 1 ]]; then
      echo SAC_API_UPGRADE_AUTO_ROLLBACK=START >&2
      /usr/bin/bash /usr/local/sbin/ecu-api-parameters-rollback ||
        echo SAC_API_UPGRADE_AUTO_ROLLBACK=FAILED >&2
    fi
  fi
}
trap on_exit EXIT
[[ "$EUID" -eq 0 && -t 0 && "$(printenv SUDO_USER || true)" == ecu ]] || {
  echo 'SAC_API_UPGRADE=DENIED interactive-operator-root-required'; exit 77;
}
[[ "$(hostname -s)" == ecu ]] || exit 2
[[ "$(runuser -u ecu -- git -C "$repo" branch --show-current)" == \
   api/v1-sac-parameters-readout-20261008 ]] || exit 2
[[ -z "$(runuser -u ecu -- git -C "$repo" status --porcelain)" ]] || exit 2
[[ -f "$stamp" && ! -L "$stamp" && -f "$candidate" &&
   ! -L "$candidate" && -x "$candidate" ]] || exit 2
read -r expected_sha expected_digest < "$stamp"
[[ "$expected_sha" =~ ^[0-9a-f]{12}$ &&
   "$expected_digest" =~ ^[0-9a-f]{64}$ ]] || exit 2
[[ "$expected_sha" == "$(runuser -u ecu -- git -C "$repo" rev-parse --short=12 HEAD)" ]] || exit 2
[[ "$expected_digest" == "$(sha256sum "$candidate" | cut -d' ' -f1)" ]] || exit 2
strings "$candidate" | grep -Fxq "$expected_sha" || exit 2
[[ -f "$destination" && ! -L "$destination" &&
   "$(stat -c '%a:%U:%G' "$destination")" == 755:root:root ]] || exit 2
[[ -f /etc/systemd/system/$unit && ! -L /etc/systemd/system/$unit ]] || exit 2
grep -Fq "ExecStart=$destination --token-file /etc/ecu-platform-v2/api/token" \
  "/etc/systemd/system/$unit" || exit 2
[[ "$(stat -c '%a:%U:%G' /etc/ecu-platform-v2/api/token)" == \
   640:root:ecu-api ]] || exit 2
[[ "$(stat -c '%a:%U:%G' /var/lib/ecu-platform-v2/api-readouts)" == \
   2750:ecu:ecu-api-read ]] || exit 2
for service in "$unit" ecu-kiosk.service ecu-webgui-static.service \
               ecu-platform-v2-bench-agent.service; do
  systemctl is-active --quiet "$service" || exit 2
done
ip -details link show can0 | grep -q 'state DOWN' || exit 2
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' \
   http://127.0.0.1:8878/api/v1/about)" == 401 ]] || exit 2
dtc=/var/lib/ecu-platform-v2/api-readouts/dtc-latest.v1
[[ -f "$dtc" && ! -L "$dtc" ]] || exit 2
previous_dtc_digest="$(sha256sum "$dtc" | cut -d' ' -f1)"
install -d -o root -g root -m 0700 /var/backups/ecu-platform-api-parameters
backup="$(mktemp -d /var/backups/ecu-platform-api-parameters/pre-XXXXXXXX)"
cp -a "$destination" "$backup/ecu_api_http"
cat > "$backup/rollback.sh" <<'RECOVERY'
#!/usr/bin/env bash
set -Eeuo pipefail
base="$(dirname "$(readlink -f "$0")")"
dest=/usr/local/libexec/ecu-platform-v2/ecu_api_http
[[ -f "$base/ecu_api_http" && ! -L "$base/ecu_api_http" ]] || exit 2
install -o root -g root -m 0755 "$base/ecu_api_http" "$dest.rollback.$$"
mv -f "$dest.rollback.$$" "$dest"
systemctl restart ecu-api-v1.service
systemctl is-active --quiet ecu-api-v1.service
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' \
   http://127.0.0.1:8878/api/v1/about)" == 401 ]] || exit 2
echo SAC_API_UPGRADE_ROLLBACK=PASS
RECOVERY
chmod 0700 "$backup/rollback.sh"
printf '#!/usr/bin/env bash\nexec /usr/bin/bash %q\n' "$backup/rollback.sh" \
  > /usr/local/sbin/ecu-api-parameters-rollback
chown root:root /usr/local/sbin/ecu-api-parameters-rollback
chmod 0700 /usr/local/sbin/ecu-api-parameters-rollback
armed=1
install -o root -g root -m 0755 "$candidate" "$destination.next.$$"
mv -f "$destination.next.$$" "$destination"
systemctl restart "$unit"
systemctl is-active --quiet "$unit"
python3 -I "$repo/scripts/check_cm5_sac_parameters_upgrade.py" \
  --expected-revision "$expected_sha"
[[ "$(sha256sum "$dtc" | cut -d' ' -f1)" == "$previous_dtc_digest" ]] || exit 2
for service in ecu-kiosk.service ecu-webgui-static.service \
               ecu-platform-v2-bench-agent.service; do
  systemctl is-active --quiet "$service" || exit 2
done
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' \
   http://127.0.0.1:8877/)" == 200 ]] || exit 2
ip -details link show can0 | grep -q 'state DOWN' || exit 2
armed=0
trap - EXIT ERR
echo "SAC_API_UPGRADE=PASS commit=$expected_sha"
echo SAC_API_UPGRADE_DTC_EVIDENCE=UNCHANGED
echo SAC_API_UPGRADE_CAN=UNCHANGED_DOWN
echo SAC_API_UPGRADE_ROLLBACK='sudo /usr/local/sbin/ecu-api-parameters-rollback'
