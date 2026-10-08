#!/usr/bin/env bash
set -Eeuo pipefail

# Stage 4.3: new DAF SAC at 500 kbit/s, physical read-only operator gate.
# The original 250k permanent bench agent and all DTC clear tooling remain
# unchanged. This script never sends 0x14, 0x11, 0x2E, 0x27 or output control.
if [[ "${EUID}" -ne 0 || ! -t 0 || ! -t 1 ]]; then
  echo "SAC_500K_READ_GATE=DENIED interactive-sudo-terminal-required" >&2
  exit 2
fi
MODE="${1:-all}"
if [[ "$#" -gt 1 || ( "$MODE" != all && "$MODE" != passive ) ]]; then
  echo "Usage: sudo bash scripts/run_stage42_daf_sac_500k_read_gate.sh [passive|all]" >&2
  exit 2
fi
TARGET_USER="${SUDO_USER:-}"
if [[ -z "$TARGET_USER" || "$TARGET_USER" == root ]]; then
  echo "SAC_500K_READ_GATE=DENIED nonroot-operator-required" >&2
  exit 2
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
IFACE=can0
BITRATE=500000
ID_PROBE="$ROOT/build/daf-sac-core-v2-probe/tests/ecu_daf_sac_core_v2_probe"
READ_PROBE="$ROOT/build/daf-sac-app-linux/tests/ecu_daf_sac_stage42_read_probe"
for tool in ip candump runuser getent cut install grep sed tee date stat chown; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "SAC_500K_READ_GATE=FAIL missing-tool=$tool" >&2
    exit 1
  }
done
if [[ "$MODE" == all && ( ! -x "$ID_PROBE" || ! -x "$READ_PROBE" ) ]]; then
  echo "SAC_500K_READ_GATE=FAIL missing-compiled-read-only-probes" >&2
  exit 1
fi
if ! ip link show "$IFACE" >/dev/null 2>&1; then
  echo "SAC_500K_READ_GATE=FAIL can0-missing" >&2
  exit 1
fi
# Fail closed without taking over an already running CAN session.
if ip -o link show "$IFACE" | grep -qE '(<|,)UP(,|>)'; then
  echo "SAC_500K_READ_GATE=BUSY can0-already-UP" >&2
  exit 4
fi
HOME_DIR="$(getent passwd "$TARGET_USER" | cut -d: -f6)"
[[ "$HOME_DIR" == /* && -d "$HOME_DIR" ]] || exit 2
EVIDENCE_DIR="$HOME_DIR/.ecu-platform-v2/daf-sac/500k-proof"
if [[ -L "$HOME_DIR/.ecu-platform-v2" ||
      -L "$HOME_DIR/.ecu-platform-v2/daf-sac" ||
      -L "$EVIDENCE_DIR" ]]; then
  echo "SAC_500K_READ_GATE=FAIL unsafe-evidence-symlink" >&2
  exit 4
fi
runuser -u "$TARGET_USER" -- install -d -m 0700 -- \
  "$HOME_DIR/.ecu-platform-v2" \
  "$HOME_DIR/.ecu-platform-v2/daf-sac" \
  "$EVIDENCE_DIR"
if ! runuser -u "$TARGET_USER" -- test -w "$EVIDENCE_DIR"; then
  echo "SAC_500K_READ_GATE=FAIL evidence-not-writable" >&2
  exit 4
fi

umask 077
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
PREFIX="$EVIDENCE_DIR/read-500k-$STAMP-$$"
PASSIVE_LOG="$PREFIX.passive.log"
SUMMARY="$PREFIX.summary.txt"
OWN_CAN=0
CAPTURE_PID=""
cleanup() {
  local rc=$?
  trap - EXIT
  if [[ -n "$CAPTURE_PID" ]]; then
    kill -TERM "$CAPTURE_PID" 2>/dev/null || true
    wait "$CAPTURE_PID" 2>/dev/null || true
  fi
  if [[ "$OWN_CAN" == 1 ]]; then
    ip link set "$IFACE" down >/dev/null 2>&1 || true
  fi
  for artifact in "$PREFIX".*; do
    if [[ -f "$artifact" && ! -L "$artifact" ]]; then
      chmod 0600 "$artifact" || true
      chown "$TARGET_USER" "$artifact" || true
    fi
  done
  if ip -o link show "$IFACE" | grep -qE '(<|,)UP(,|>)'; then
    echo "SAC_500K_CAN_CLEANUP=FAILED" >&2
    exit 9
  fi
  echo "SAC_500K_CAN_CLEANUP=DOWN"
  exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

OWN_CAN=1
echo "[ECU V2] DAF SAC 500k isolated DUT proof ($MODE)" | tee "$SUMMARY"
echo "SAC_500K_TX_POLICY=READ_ONLY_NO_CLEAR_NO_WRITE_NO_ACTUATION" | tee -a "$SUMMARY"

# 1. Passive discovery. Listening at 500k does not send ACK or UDS.
ip link set "$IFACE" down
ip link set "$IFACE" type can bitrate "$BITRATE" fd off listen-only on
RX_BEFORE="$(cat "/sys/class/net/$IFACE/statistics/rx_packets")"
ERR_BEFORE="$(cat "/sys/class/net/$IFACE/statistics/rx_errors")"
TX_BEFORE="$(cat "/sys/class/net/$IFACE/statistics/tx_packets")"
# -D lets candump remain attached until the controller is enabled.
candump -D -ta -e "$IFACE,0:0,#FFFFFFFF" > "$PASSIVE_LOG" 2>&1 &
CAPTURE_PID=$!
sleep 0.25
if ! kill -0 "$CAPTURE_PID" 2>/dev/null; then
  echo "SAC_500K_PASSIVE=FAIL sniffer-not-running" | tee -a "$SUMMARY"
  exit 1
fi
ip link set "$IFACE" up
if ! ip -details link show "$IFACE" | grep -q 'LISTEN-ONLY'; then
  echo "SAC_500K_PASSIVE=FAIL link-not-listen-only" | tee -a "$SUMMARY"
  exit 1
fi
echo "SAC_500K_PASSIVE_WINDOW_SEC=5" | tee -a "$SUMMARY"
sleep 5
RX_AFTER="$(cat "/sys/class/net/$IFACE/statistics/rx_packets")"
ERR_AFTER="$(cat "/sys/class/net/$IFACE/statistics/rx_errors")"
TX_AFTER="$(cat "/sys/class/net/$IFACE/statistics/tx_packets")"
kill -TERM "$CAPTURE_PID" 2>/dev/null || true
wait "$CAPTURE_PID" 2>/dev/null || true
CAPTURE_PID=""
DATA_FRAMES="$(grep -Ec 'can0[[:space:]]+[0-9A-Fa-f]{3,8}[[:space:]]+\[[[:space:]]*[0-9]+\]' "$PASSIVE_LOG" || true)"
ERROR_FRAMES="$(grep -c 'ERRORFRAME' "$PASSIVE_LOG" || true)"
echo "SAC_500K_PASSIVE_RX_PACKETS_DELTA=$((RX_AFTER - RX_BEFORE))" | tee -a "$SUMMARY"
echo "SAC_500K_PASSIVE_RX_ERRORS_DELTA=$((ERR_AFTER - ERR_BEFORE))" | tee -a "$SUMMARY"
echo "SAC_500K_PASSIVE_TX_PACKETS_DELTA=$((TX_AFTER - TX_BEFORE))" | tee -a "$SUMMARY"
echo "SAC_500K_PASSIVE_DATA_FRAMES=$DATA_FRAMES" | tee -a "$SUMMARY"
echo "SAC_500K_PASSIVE_ERROR_FRAMES=$ERROR_FRAMES" | tee -a "$SUMMARY"
ip link set "$IFACE" down
if (( ERR_AFTER > ERR_BEFORE || TX_AFTER > TX_BEFORE || ERROR_FRAMES > 0 )); then
  echo "SAC_500K_PASSIVE=FAIL physical-errors-or-unexpected-transmission" | tee -a "$SUMMARY"
  exit 1
fi
if (( DATA_FRAMES == 0 )); then
  echo "SAC_500K_PASSIVE=INCONCLUSIVE_NO_BROADCAST" | tee -a "$SUMMARY"
else
  echo "SAC_500K_PASSIVE=RX_OBSERVED" | tee -a "$SUMMARY"
fi
if [[ "$MODE" == passive ]]; then
  echo "SAC_500K_READ_GATE=PASSIVE_COMPLETE_NO_TX" | tee -a "$SUMMARY"
  echo "SAC_500K_EVIDENCE=$PREFIX"
  exit 0
fi

# 2. User explicitly selected a 500k ECU. A lack of J1939 broadcast from
# an isolated bench ECU is inconclusive, not permission to guess 250k.
# Active probing stops on the first failed read-only identification.
ip link set "$IFACE" type can bitrate "$BITRATE" fd off listen-only off
ip link set "$IFACE" up
LINK_INFO="$PREFIX.can-before.txt"
ip -details -statistics link show "$IFACE" > "$LINK_INFO"
if ! grep -q 'bitrate 500000' "$LINK_INFO" ||
   grep -q 'BUS-OFF' "$LINK_INFO" ||
   grep -q 'LISTEN-ONLY' "$LINK_INFO"; then
  echo "SAC_500K_READ_GATE=FAIL wrong-active-link-profile" | tee -a "$SUMMARY"
  exit 1
fi
echo "SAC_500K_READ_STAGE=IDENTIFICATION" | tee -a "$SUMMARY"
if ! runuser -u "$TARGET_USER" -- "$ID_PROBE" "$IFACE" "$BITRATE" 2>&1 \
    | sed -E 's/^(SAC_VIN=).*/\1[REDACTED]/' \
    | tee "$PREFIX.identify.txt"; then
  echo "SAC_500K_IDENTIFY=FAIL (no further requests)" | tee -a "$SUMMARY"
  exit 1
fi
if ! grep -q '^SAC_PHYSICAL_PROBE=PASS' "$PREFIX.identify.txt"; then
  echo "SAC_500K_IDENTIFY=FAIL missing-PASS" | tee -a "$SUMMARY"
  exit 1
fi
echo "SAC_500K_IDENTIFY=PASS" | tee -a "$SUMMARY"

READ_FAILURES=0
for mode in parameters dtc; do
  echo "SAC_500K_READ_STAGE=$mode" | tee -a "$SUMMARY"
  if runuser -u "$TARGET_USER" -- "$READ_PROBE" "$IFACE" "$mode" "$BITRATE" 2>&1 \
      | tee "$PREFIX.$mode.txt"; then
    if grep -q '^SAC_STAGE42_READ_PHYSICAL=PASS$' "$PREFIX.$mode.txt"; then
      echo "SAC_500K_READ_RESULT=PASS mode=$mode" | tee -a "$SUMMARY"
    else
      echo "SAC_500K_READ_RESULT=FAIL mode=$mode missing-PASS" | tee -a "$SUMMARY"
      READ_FAILURES=$((READ_FAILURES + 1))
    fi
  else
    echo "SAC_500K_READ_RESULT=FAIL mode=$mode" | tee -a "$SUMMARY"
    READ_FAILURES=$((READ_FAILURES + 1))
  fi
  # Different SAC firmware may not implement FE96; the next independent
  # read-only DTC query remains valuable. Stop if the bus itself goes BUS-OFF.
  if ip -details link show "$IFACE" | grep -q 'BUS-OFF'; then
    echo "SAC_500K_READ_GATE=FAIL bus-off" | tee -a "$SUMMARY"
    exit 1
  fi
done

ip -details -statistics link show "$IFACE" > "$PREFIX.can-after.txt"
echo "SAC_500K_EVIDENCE=$PREFIX" | tee -a "$SUMMARY"
if (( READ_FAILURES != 0 )); then
  echo "SAC_500K_READ_GATE=PARTIAL identify-pass-read-failures=$READ_FAILURES" | tee -a "$SUMMARY"
  exit 1
fi
echo "SAC_500K_READ_GATE=PASS identity-voltage-DTC" | tee -a "$SUMMARY"
