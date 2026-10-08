#!/usr/bin/env bash
set -euo pipefail

# Stage 3.3A DAF SAC physical discovery. Receive-only: no CAN TX.
IFACE="can0"
WINDOW_SECONDS=8

if [[ "${EUID}" -ne 0 ]]; then
  echo "ERROR: run with sudo"
  exit 1
fi

for cmd in ip candump mktemp; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "ERROR: required command missing: $cmd"
    exit 1
  fi
done

umask 077
TMP_DIR="$(mktemp -d /tmp/ecu-sac-passive.XXXXXXXX)"
CANDUMP_PID=""

cleanup() {
  local exit_code=$?
  if [[ -n "$CANDUMP_PID" ]]; then
    kill -TERM "$CANDUMP_PID" 2>/dev/null || true
    wait "$CANDUMP_PID" 2>/dev/null || true
    CANDUMP_PID=""
  fi
  ip link set "$IFACE" down >/dev/null 2>&1 || true
  rm -rf -- "$TMP_DIR"
  return "$exit_code"
}
trap cleanup EXIT

echo "[ECU V2] Stage 3.3A — DAF SAC passive bitrate discovery (capture fixed)"
echo "Receive-only: LISTEN-ONLY, Classic CAN, no diagnostic or CAN TX."
echo "Each capture lasts ${WINDOW_SECONDS}s after the interface is UP."
echo

probe_bitrate() {
  local bitrate="$1"
  local capture="$TMP_DIR/can-${bitrate}.log"
  local rxp0 rxe0 rxb0 txp0 rxp1 rxe1 rxb1 txp1
  local logged error_frames data_frames

  ip link set "$IFACE" down
  ip link set "$IFACE" type can bitrate "$bitrate" fd off listen-only on

  rxp0="$(cat "/sys/class/net/$IFACE/statistics/rx_packets")"
  rxe0="$(cat "/sys/class/net/$IFACE/statistics/rx_errors")"
  rxb0="$(cat "/sys/class/net/$IFACE/statistics/rx_bytes")"
  txp0="$(cat "/sys/class/net/$IFACE/statistics/tx_packets")"

  # IMPORTANT: -L and -e cannot be used together with this candump version.
  # -D keeps a monitor started while the interface is DOWN attached.
  # Arm the raw CAN listener before bringing the interface UP to avoid losing
  # early frames during CAN controller/link initialization.
  candump -D -ta -e "$IFACE,0:0,#FFFFFFFF" >"$capture" 2>&1 &
  CANDUMP_PID=$!
  sleep 0.25
  if ! kill -0 "$CANDUMP_PID" 2>/dev/null; then
    echo "ERROR: candump exited before CAN link activation at $bitrate"
    cat "$capture"
    exit 1
  fi

  ip link set "$IFACE" up

  echo "=== PASSIVE $bitrate bit/s ==="
  ip -details link show "$IFACE" | sed -n '1,8p'
  if ! ip -details link show "$IFACE" | grep -q '<LISTEN-ONLY>'; then
    echo "ERROR: CAN controller is not in LISTEN-ONLY mode"
    exit 1
  fi

  sleep "$WINDOW_SECONDS"

  rxp1="$(cat "/sys/class/net/$IFACE/statistics/rx_packets")"
  rxe1="$(cat "/sys/class/net/$IFACE/statistics/rx_errors")"
  rxb1="$(cat "/sys/class/net/$IFACE/statistics/rx_bytes")"
  txp1="$(cat "/sys/class/net/$IFACE/statistics/tx_packets")"

  if ! kill -0 "$CANDUMP_PID" 2>/dev/null; then
    echo "ERROR: candump exited during the capture window at $bitrate"
    cat "$capture"
    exit 1
  fi
  kill -TERM "$CANDUMP_PID" 2>/dev/null || true
  wait "$CANDUMP_PID" || true
  CANDUMP_PID=""

  # Exclude lifecycle lines such as 'can0: interface down'.
  logged="$(grep -Ec 'can0[[:space:]]+[0-9A-Fa-f]{3,8}[[:space:]]+\[[[:space:]]*[0-9]+\]' "$capture" || true)"
  error_frames="$(grep -c 'ERRORFRAME' "$capture" || true)"
  data_frames=$((logged - error_frames))
  if (( data_frames < 0 )); then
    data_frames=0
  fi

  echo "SAC_PASSIVE_BITRATE=$bitrate"
  echo "SAC_PASSIVE_DATA_FRAMES=$data_frames"
  echo "SAC_PASSIVE_ERROR_FRAMES=$error_frames"
  echo "SAC_PASSIVE_RX_PACKETS_DELTA=$((rxp1-rxp0))"
  echo "SAC_PASSIVE_RX_BYTES_DELTA=$((rxb1-rxb0))"
  echo "SAC_PASSIVE_RX_ERRORS_DELTA=$((rxe1-rxe0))"
  echo "SAC_PASSIVE_TX_PACKETS_DELTA=$((txp1-txp0))"
  echo "SAC_PASSIVE_BERR_AFTER=$(ip -details link show "$IFACE" | grep -o 'berr-counter tx [0-9]* rx [0-9]*' || true)"

  echo "--- candump capture (first 40 lines, including diagnostics) ---"
  sed -n '1,40p' "$capture"
  if (( (rxp1-rxp0) > 0 && data_frames == 0 && error_frames == 0 )); then
    echo "SAC_PASSIVE_CAPTURE_MISMATCH=YES (kernel RX increased without captured CAN frames)"
  else
    echo "SAC_PASSIVE_CAPTURE_MISMATCH=NO"
  fi
  echo

  ip link set "$IFACE" down
}

probe_bitrate 250000
probe_bitrate 500000

echo "DAF_SAC_PASSIVE_BITRATE_DISCOVERY=COMPLETE"
echo "Compare correctly captured data frames, error frames and kernel RX counters."
echo "The interface will remain DOWN."
