#!/usr/bin/env bash
# Scoped physical SAC identification parser hotfix, operator-only.
# No Core, CAN configuration, V1 API or GUI replacement.
set -Eeuo pipefail
umask 077

repo=/home/ecu/ECU_WEBGUI_PARAMS_V1
source_file="$repo/deploy/sac_connect/sac_identify_server.py"
target=/usr/local/libexec/ecu-platform-v2/sac_identify_server.py
backup_root=/var/backups/ecu-platform-v2-sac-identity-parser
service=ecu-sac-connect-v1.service
expected_old_sha=10923ebd8fb02926be9371f0e8c58aebaee00a60a80efe7037145aa709ee9494
backup=""
armed=0

on_exit() {
  local rc=$?
  trap - EXIT
  if (( rc != 0 )); then
    echo "SAC_IDENTITY_HOTFIX=FAIL exit=$rc" >&2
    if (( armed == 1 )); then
      bash "$backup/rollback.sh" ||
        echo "SAC_IDENTITY_ROLLBACK=FAILED manual-recovery-required" >&2
    fi
  fi
}
trap on_exit EXIT

[[ "$EUID" -eq 0 && -t 0 && "${SUDO_USER:-}" == ecu ]] || {
  echo "SAC_IDENTITY_HOTFIX=DENIED interactive-ecu-sudo-required"
  exit 77
}
[[ "$(hostname -s)" == ecu ]] || exit 2
[[ "$(runuser -u ecu -- git -C "$repo" branch --show-current)" == webgui/sac-local-prototype-no-token-20261009 ]] || exit 2
[[ -z "$(runuser -u ecu -- git -C "$repo" status --porcelain)" ]] || exit 2
[[ -f "$source_file" && ! -L "$source_file" ]] || exit 2
[[ -f "$target" && ! -L "$target" && "$(stat -c %U:%G "$target")" == root:root ]] || exit 2
[[ "$(sha256sum "$target" | cut -d' ' -f1)" == "$expected_old_sha" ]] || {
  echo "SAC_IDENTITY_HOTFIX=REFUSED unexpected-installed-binary"
  exit 2
}
[[ "$(systemctl show -p ExecStart --value "$service")" == *"/usr/local/libexec/ecu-platform-v2/sac_identify_server.py"* ]] || exit 2
for unit in "$service" ecu-api-v1.service ecu-webgui-static.service \
            ecu-kiosk.service ecu-platform-v2-bench-agent.service; do
  systemctl is-active --quiet "$unit" || exit 2
done
ip -details link show can0 | grep -q 'state DOWN' || exit 2
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:8879/api/v1/bench/daf-sac/connect)" == 401 ]] || exit 2

runuser -u ecu -- python3 -m unittest discover -s "$repo/tests"   -p test_sac_connect_adapter.py -q
python3 -m py_compile "$source_file"

dtc_file=/var/lib/ecu-platform-v2/api-readouts/dtc-latest.v1
[[ -f "$dtc_file" ]] || exit 2
dtc_sha="$(sha256sum "$dtc_file" | cut -d' ' -f1)"

install -d -o root -g root -m 0700 "$backup_root"
backup="$(mktemp -d "$backup_root/pre-XXXXXXXX")"
install -o root -g root -m 0700 "$target" "$backup/original.py"
cat > "$backup/rollback.sh" <<'RECOVER'
#!/usr/bin/env bash
set -Eeuo pipefail
dir="$(dirname "$(readlink -f "$0")")"
target=/usr/local/libexec/ecu-platform-v2/sac_identify_server.py
install -o root -g root -m 0755 "$dir/original.py" "$target.rollback-$$"
mv -f "$target.rollback-$$" "$target"
systemctl restart ecu-sac-connect-v1.service
systemctl is-active --quiet ecu-sac-connect-v1.service
ip -details link show can0 | grep -q 'state DOWN'
echo "SAC_IDENTITY_ROLLBACK=PASS"
RECOVER
chmod 0700 "$backup/rollback.sh"
printf '#!/usr/bin/env bash\nexec /usr/bin/bash %q\n' "$backup/rollback.sh"   > /usr/local/sbin/ecu-sac-identity-parser-rollback
chmod 0700 /usr/local/sbin/ecu-sac-identity-parser-rollback
armed=1

install -o root -g root -m 0755 "$source_file" "$target.next-$$"
mv -f "$target.next-$$" "$target"
systemctl restart "$service"
systemctl is-active --quiet "$service"
[[ "$(sha256sum "$target" | cut -d' ' -f1)" == "$(sha256sum "$source_file" | cut -d' ' -f1)" ]] || exit 2
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:8879/api/v1/bench/daf-sac/connect)" == 401 ]] || exit 2
[[ "$(curl -s --max-time 3 -o /dev/null -w '%{http_code}' -H 'X-ECU-Kiosk: v1' http://127.0.0.1:8877/kiosk/v1/about)" == 200 ]] || exit 2
[[ "$(sha256sum "$dtc_file" | cut -d' ' -f1)" == "$dtc_sha" ]] || exit 2
ip -details link show can0 | grep -q 'state DOWN' || exit 2

armed=0
trap - EXIT
echo "SAC_IDENTITY_HOTFIX=PASS parser-start-pass"
echo "SAC_IDENTITY_HOTFIX_CAN=UNCHANGED_DOWN"
echo "SAC_IDENTITY_HOTFIX_DTC=UNCHANGED"
echo "SAC_IDENTITY_HOTFIX_ROLLBACK=sudo /usr/local/sbin/ecu-sac-identity-parser-rollback"
