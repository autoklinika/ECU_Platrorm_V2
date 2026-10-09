#!/usr/bin/env bash
# Deploy one CSS-only presentation change. No kiosk reinstall, no API/Bench changes.
set -Eeuo pipefail

[[ "$EUID" -eq 0 ]] || { echo "CURSOR_UPDATE=NEEDS_ROOT"; exit 77; }

repo="/home/ecu/ECU_WebGUI_Home_V1"
base="/opt/ecu-platform/webgui"
current="$base/current"
source_css="$repo/webgui/styles.css"

[[ -L "$current" && -f "$source_css" ]] || { echo "CURSOR_UPDATE=MISSING_ASSETS"; exit 2; }
[[ "$(systemctl show -p User --value ecu-kiosk.service)" == "ecu-kiosk" ]] ||
  { echo "CURSOR_UPDATE=WRONG_KIOSK_USER"; exit 2; }
systemctl is-active --quiet ecu-kiosk.service
systemctl is-active --quiet ecu-webgui-static.service
grep -Fq 'cursor: none !important;' "$source_css"

old_target="$(readlink "$current")"
case "$old_target" in
  releases/*) ;;
  *) echo "CURSOR_UPDATE=UNKNOWN_RELEASE"; exit 2 ;;
esac

test -z "$(git -C "$repo" status --porcelain)" || { echo "CURSOR_UPDATE=DIRTY_SOURCE"; exit 2; }
revision="$(git -C "$repo" rev-parse --short=12 HEAD)"
new_target="releases/$revision"
new_dir="$base/$new_target"

if [[ "$old_target" == "$new_target" ]]; then
  echo "CURSOR_UPDATE=ALREADY_CURRENT"
  exit 0
fi
[[ ! -e "$new_dir" ]] || { echo "CURSOR_UPDATE=RELEASE_EXISTS"; exit 2; }

staged="$(mktemp -d "$base/releases/.cursor-XXXXXXXX")"
switched=0
rollback() {
  local exit_code=$?
  trap - EXIT
  if [[ "$exit_code" -ne 0 ]]; then
    if [[ "$switched" -eq 1 ]]; then
      ln -s "$old_target" "$base/.previous-cursor-$$"
      mv -Tf "$base/.previous-cursor-$$" "$current"
      systemctl restart ecu-kiosk.service ||
        echo "CURSOR_UPDATE=ROLLBACK_KIOSK_RESTART_FAILED" >&2
      echo "CURSOR_UPDATE=ROLLED_BACK" >&2
    fi
    [[ -d "$staged" ]] && rm -rf -- "$staged"
  fi
}
trap rollback EXIT

cp -a "$current/." "$staged/"
chmod 0755 "$staged"
install -o root -g root -m 0644 "$source_css" "$staged/styles.css"
cmp -s "$source_css" "$staged/styles.css"
mv -T "$staged" "$new_dir"

ln -s "$new_target" "$base/.next-cursor-$$"
mv -Tf "$base/.next-cursor-$$" "$current"
switched=1

curl -fsS --max-time 3 "http://127.0.0.1:8877/styles.css" |
  grep -Fq 'cursor: none !important;'
systemctl restart ecu-kiosk.service
sleep 2
systemctl is-active --quiet ecu-kiosk.service
[[ "$(systemctl show -p User --value ecu-kiosk.service)" == "ecu-kiosk" ]]
pgrep -u ecu-kiosk -x chromium >/dev/null

echo "CURSOR_UPDATE=PASS"
echo "NEW_RELEASE=$new_target"
echo "KIOSK=ACTIVE"
echo "API_BENCH_CORE=UNCHANGED"
echo "Physical pointer visibility to be confirmed on the touchscreen."
trap - EXIT
