#!/usr/bin/env bash
set -Eeuo pipefail
[[ "${EUID}" -eq 0 ]] || { echo "ECU_WEBGUI_ROLLBACK=NEEDS_ROOT"; exit 77; }
backup="${1:-}"
case "$backup" in /var/backups/ecu-platform-kiosk/*) ;; *) echo "INVALID_BACKUP_PATH"; exit 2;; esac
[[ -r "$backup/ecu-kiosk.service" && -r "$backup/ecu-kiosk.default" ]] || { echo "INCOMPLETE_KIOSK_BACKUP"; exit 2; }
echo "ROLLBACK_SOURCE=$backup"
systemctl stop ecu-kiosk.service || true
systemctl stop ecu-webgui-static.service || true
systemctl disable ecu-webgui-static.service || true
install -m 0644 "$backup/ecu-kiosk.service" /etc/systemd/system/ecu-kiosk.service
install -m 0644 "$backup/ecu-kiosk.default" /etc/default/ecu-kiosk
if [[ -f "$backup/ecu-webgui-static.service" ]]; then
  install -m 0644 "$backup/ecu-webgui-static.service" /etc/systemd/system/ecu-webgui-static.service
else
  rm -f /etc/systemd/system/ecu-webgui-static.service
fi
if [[ -f "$backup/91-ecu-kiosk-touch.rules" ]]; then
  install -m 0644 "$backup/91-ecu-kiosk-touch.rules" /etc/udev/rules.d/91-ecu-kiosk-touch.rules
else
  rm -f /etc/udev/rules.d/91-ecu-kiosk-touch.rules
fi
if [[ -f "$backup/webgui-current.link" ]]; then
  ln -sfn "$(cat "$backup/webgui-current.link")" /opt/ecu-platform/webgui/current
else
  rm -f /opt/ecu-platform/webgui/current
fi
udevadm control --reload || true
udevadm trigger --action=change --subsystem-match=input || true
udevadm settle || true
# A rollback must restore the old ecu user's touchscreen privileges too.
for event in /dev/input/event*; do
  [[ -e "$event" ]] || continue
  properties="$(udevadm info --query=property --name="$event" 2>/dev/null || true)"
  if grep -qx 'ID_VENDOR_ID=0712' <<< "$properties" &&
     grep -qx 'ID_MODEL_ID=0009' <<< "$properties" &&
     grep -qx 'ID_INPUT_TOUCHSCREEN=1' <<< "$properties"; then
    chgrp input "$event"
    chmod 0660 "$event"
  fi
done
systemctl daemon-reload
systemctl enable ecu-kiosk.service
systemctl restart ecu-kiosk.service
systemctl is-active --quiet ecu-kiosk.service
echo "ECU_WEBGUI_ROLLBACK=PASS"
echo "Original kiosk service restored. The isolated kiosk account is retained but unused."
