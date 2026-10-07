#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "ERROR: run with sudo"
  exit 1
fi

IFACE="can0"
WINDOW_SECONDS=3
TMP_DIR="/tmp/ecu-sac-passive-$$"

cleanup() {
  ip link set "$IFACE" down >/dev/null 2>&1 || true
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT
mkdir -p "$TMP_DIR"

if ! command -v candump >/dev/null 2>&1; then
  echo "ERROR: candump is required"
  exit 1
fi

echo "[ECU V2] Stage 3.3A — DAF SAC passive bitrate discovery"
echo "NO diagnostic request will be transmitted."
echo "The CAN controller is forced to LISTEN-ONLY for both candidates."
echo

probe_bitrate() {
  local bitrate="$1"
  local capture="$TMP_DIR/can-${bitrate}.log"

  ip link set "$IFACE" down
  ip link set "$IFACE" type can bitrate "$bitrate" fd off listen-only on
  ip link set "$IFACE" up

  local rxp0 rxe0 rxp1 rxe1
  rxp0="$(cat /sys/class/net/$IFACE/statistics/rx_packets)"
  rxe0="$(cat /sys/class/net/$IFACE/statistics/rx_errors)"

  echo "=== PASSIVE $bitrate bit/s ==="
  ip -details link show "$IFACE" | sed -n '1,8p'

  set +e
  timeout "${WINDOW_SECONDS}s" candump -L -e "$IFACE,0:0,#FFFFFFFF" >"$capture" 2>&1
  local dump_rc=$?
  set -e
  if [[ "$dump_rc" -ne 0 && "$dump_rc" -ne 124 ]]; then
    echo "WARN: candump exit=$dump_rc"
  fi

  rxp1="$(cat /sys/class/net/$IFACE/statistics/rx_packets)"
  rxe1="$(cat /sys/class/net/$IFACE/statistics/rx_errors)"

  local total error_frames data_frames
  total="$(grep -c 'can0' "$capture" 2>/dev/null || true)"
  error_frames="$(grep -c 'ERRORFRAME' "$capture" 2>/dev/null || true)"
  data_frames=$(( total - error_frames ))

  echo "SAC_PASSIVE_BITRATE=$bitrate"
  echo "SAC_PASSIVE_DATA_FRAMES=$data_frames"
  echo "SAC_PASSIVE_ERROR_FRAMES=$error_frames"
  echo "SAC_PASSIVE_RX_PACKETS_DELTA=$((rxp1-rxp0))"
  echo "SAC_PASSIVE_RX_ERRORS_DELTA=$((rxe1-rxe0))"
  echo "SAC_PASSIVE_BERR_AFTER=$(ip -details link show "$IFACE" | grep -o 'berr-counter tx [0-9]* rx [0-9]*' || true)"

  if [[ "$total" -gt 0 ]]; then
    echo "--- first observed frames ---"
    head -20 "$capture"
  fi
  echo

  ip link set "$IFACE" down
}

probe_bitrate 250000
probe_bitrate 500000

echo "DAF_SAC_PASSIVE_BITRATE_DISCOVERY=COMPLETE"
echo "Interpretation: prefer the bitrate with valid data frames and without growing RX errors."
echo "Interface will remain DOWN."
