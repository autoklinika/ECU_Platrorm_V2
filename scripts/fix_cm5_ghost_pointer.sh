#!/usr/bin/env bash
# CM5 kiosk: remove ghost mouse cursor by ignoring ONLY two HDMI-CEC
# virtual pointer devices in libinput. Touch, browser isolation, API and
# Bench Runtime are unchanged.
set -Eeuo pipefail

[[ "$EUID" -eq 0 ]] || { echo "HDMI_POINTER_FIX=NEEDS_ROOT"; exit 77; }
[[ "$(hostname -s)" == "ecu" ]] || { echo "HDMI_POINTER_FIX=WRONG_HOST"; exit 2; }
[[ "$(systemctl show -p User --value ecu-kiosk.service)" == "ecu-kiosk" ]] ||
  { echo "HDMI_POINTER_FIX=WRONG_KIOSK_IDENTITY"; exit 2; }
systemctl is-active --quiet ecu-kiosk.service
systemctl is-active --quiet ecu-webgui-static.service

mode="${1:-apply}"
[[ "$mode" == "apply" || "$mode" == "--rollback" ]] ||
  { echo "Usage: sudo bash scripts/fix_cm5_ghost_pointer.sh [--rollback]"; exit 2; }

source_rule="/home/ecu/ECU_WebGUI_Home_V1/deploy/webgui/90-ecu-kiosk-ignore-hdmi-pointer.rules"
installed_rule="/etc/udev/rules.d/90-ecu-kiosk-ignore-hdmi-pointer.rules"
[[ -r "$source_rule" ]] || exit 2
udevadm verify "$source_rule" >/dev/null
if [[ -e "$installed_rule" ]] && ! cmp -s "$source_rule" "$installed_rule"; then
  echo "HDMI_POINTER_FIX=CONFLICTING_EXISTING_RULE"
  exit 2
fi

declare -a targets=()
touch_seen=0
for event in /dev/input/event*; do
  [[ -c "$event" ]] || continue
  name="$(cat "/sys/class/input/${event##*/}/device/name")"
  properties="$(udevadm info --query=property --name="$event")"
  case "$name" in
    vc4-hdmi-0|vc4-hdmi-1)
      grep -qx 'ID_INPUT_POINTINGSTICK=1' <<< "$properties" ||
        { echo "HDMI_POINTER_FIX=NOT_POINTING_DEVICE $event"; exit 2; }
      targets+=("$event")
      ;;
    *)
      if grep -qx 'ID_INPUT_TOUCHSCREEN=1' <<< "$properties" &&
         grep -qx 'ID_VENDOR_ID=0712' <<< "$properties" &&
         grep -qx 'ID_MODEL_ID=0009' <<< "$properties"; then
        ((touch_seen+=1))
        [[ ! "$properties" =~ LIBINPUT_IGNORE_DEVICE=1 ]] ||
          { echo "HDMI_POINTER_FIX=TOUCH_ALREADY_IGNORED"; exit 2; }
      fi
      ;;
  esac
done
[[ "${#targets[@]}" -eq 2 && "$touch_seen" -eq 1 ]] ||
  { echo "HDMI_POINTER_FIX=DEVICE_PRECHECK_FAILED"; exit 2; }

reload_hdmi() {
  udevadm control --reload
  for event in "${targets[@]}"; do
    udevadm trigger --action=change "/sys/class/input/${event##*/}"
  done
  udevadm settle
}
created=0
cleanup_on_failure() {
  result=$?
  trap - EXIT
  if [[ "$result" -ne 0 && "$created" -eq 1 ]]; then
    rm -f "$installed_rule"
    reload_hdmi || true
    systemctl restart ecu-kiosk.service || true
    echo "HDMI_POINTER_FIX=AUTO_ROLLBACK" >&2
  fi
}
trap cleanup_on_failure EXIT

if [[ "$mode" == "--rollback" ]]; then
  [[ -f "$installed_rule" ]] || { echo "HDMI_POINTER_FIX=ALREADY_ROLLED_BACK"; exit 0; }
  rm -f "$installed_rule"
  reload_hdmi
  systemctl restart ecu-kiosk.service
  systemctl is-active --quiet ecu-kiosk.service
  echo "HDMI_POINTER_FIX=ROLLED_BACK"
  trap - EXIT
  exit 0
fi

if [[ ! -f "$installed_rule" ]]; then
  install -o root -g root -m 0644 "$source_rule" "$installed_rule"
  created=1
fi
reload_hdmi
for event in "${targets[@]}"; do
  udevadm info --query=property --name="$event" |
    grep -qx 'LIBINPUT_IGNORE_DEVICE=1' ||
    { echo "HDMI_POINTER_FIX=RULE_NOT_APPLIED $event"; exit 1; }
done
# Don't retrigger, disable or modify the WaveShare touch device.
systemctl restart ecu-kiosk.service
sleep 2
systemctl is-active --quiet ecu-kiosk.service
[[ "$(systemctl show -p User --value ecu-kiosk.service)" == "ecu-kiosk" ]]
pgrep -u ecu-kiosk -x chromium >/dev/null

echo "HDMI_POINTER_FIX=PASS"
echo "VIRTUAL_HDMI_POINTERS=IGNORED"
echo "WAVESHARE_TOUCH=UNCHANGED"
echo "BENCH_CORE_API=UNCHANGED"
echo "ROLLBACK=sudo bash /home/ecu/ECU_WebGUI_Home_V1/scripts/fix_cm5_ghost_pointer.sh --rollback"
trap - EXIT
