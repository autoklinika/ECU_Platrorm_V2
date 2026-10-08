#!/usr/bin/env bash
# Explicit rollback of first standalone CM5 API V1 deployment.
# Does not remove operator data, credentials, accounts or privileged Bench Agent.
set -Eeuo pipefail

if [[ ${EUID} -ne 0 ]]; then
  echo "ECU_API_ROLLBACK=DENIED root-required" >&2
  exit 2
fi
if [[ "${1:-}" != "" && "${1:-}" != "--automatic" ]]; then
  echo "ECU_API_ROLLBACK=DENIED invalid-argument" >&2
  exit 2
fi

STATE=/var/lib/ecu-platform-v2/api-install-state/installed-v1
UNIT=/etc/systemd/system/ecu-api-v1.service
BINARY=/usr/local/libexec/ecu-platform-v2/ecu_api_http

if [[ ! -f "$STATE" || -L "$STATE" ||
      "$(stat -c '%U:%G %a' "$STATE")" != "root:root 600" ]]; then
  echo "ECU_API_ROLLBACK=DENIED no-trusted-install-marker" >&2
  exit 3
fi
if [[ -L "$UNIT" || -L "$BINARY" ]]; then
  echo "ECU_API_ROLLBACK=DENIED unexpected-runtime-symlink" >&2
  exit 3
fi
if [[ -e "$UNIT" ]] &&
    ! grep -qF 'Description=ECU Platform V2 Application API V1 (read-only)' "$UNIT"; then
  echo "ECU_API_ROLLBACK=DENIED foreign-systemd-unit" >&2
  exit 3
fi
TX_BEFORE="$(cat /sys/class/net/can0/statistics/tx_packets)"
systemctl stop ecu-api-v1.service || true
systemctl disable ecu-api-v1.service >/dev/null 2>&1 || true
if [[ -f "$UNIT" ]]; then
  rm -- "$UNIT"
fi
systemctl daemon-reload
systemctl reset-failed ecu-api-v1.service >/dev/null 2>&1 || true
if [[ -e "$BINARY" && ! -L "$BINARY" ]]; then
  rm -- "$BINARY"
fi
rm -- "$STATE"

# Preserve future operator control: do not remove /etc credentials,
# the readout journal, the dedicated system account, or rollback executable.
for service in ecu-kiosk.service ecu-webgui-static.service \
               ecu-platform-v2-bench-agent.service; do
  if [[ "$(systemctl is-active "$service")" != active ]]; then
    echo "ECU_API_ROLLBACK=WARNING baseline-service=$service" >&2
  fi
done
[[ "$(cat /sys/class/net/can0/statistics/tx_packets)" == "$TX_BEFORE" ]] || {
  echo "ECU_API_ROLLBACK=FAIL CAN_TX_COUNTER_CHANGED" >&2
  exit 4
}
echo "ECU_API_ROLLBACK=PASS service-disabled runtime-removed"
echo "ECU_API_ROLLBACK_DATA=RETAINED"
