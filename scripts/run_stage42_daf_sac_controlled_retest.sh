#!/usr/bin/env bash
set -euo pipefail

# ONLY for a separately approved, physically attended, one-shot retest
# after a previous DAF SAC 0x14 outcome UNKNOWN.
#
# Does not weaken the normal clear gate, modify past evidence or enable
# remote/unattended erasing. No UDS 0x14 on startup or during dry-runs/CI.
if [[ "$#" -ne 0 || "${EUID}" -ne 0 ||
      ! -t 0 || ! -t 1 ||
      -z "${SUDO_USER:-}" || "${SUDO_USER}" == root ]]; then
  echo "ERROR: run ONLY as sudo from a live local operator TTY; no arguments" >&2
  exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROBE="$ROOT_DIR/build/daf-sac-app-linux/tests/ecu_daf_sac_stage42_clear_probe"
AUTHORIZE="$ROOT_DIR/scripts/authorize_daf_sac_controlled_retest.py"
ANALYZE="$ROOT_DIR/scripts/analyze_daf_sac_dtc_trace.py"
IFACE=can0
TARGET_USER="$SUDO_USER"
umask 077

for tool in ip runuser getent cut install candump stdbuf script python3 \
            sha256sum date chown chmod grep tee; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "ERROR: missing tool $tool" >&2
    exit 1
  }
done
if [[ ! -x "$PROBE" || ! -f "$AUTHORIZE" || ! -f "$ANALYZE" ]]; then
  echo "ERROR: test binary/helper absent; build and validate offline first" >&2
  exit 1
fi

HOME_DIR="$(getent passwd "$TARGET_USER" | cut -d: -f6)"
[[ "$HOME_DIR" == /* ]] || { echo "ERROR: operator home invalid" >&2; exit 1; }
ROOT="$HOME_DIR/.ecu-platform-v2"
SAC_DIR="$ROOT/daf-sac"
EVIDENCE_DIR="$SAC_DIR/dtc-clear"
CAPTURE_DIR="$SAC_DIR/dtc-capture"
if [[ -L "$ROOT" || -L "$SAC_DIR" || -L "$EVIDENCE_DIR" ||
      -L "$CAPTURE_DIR" ]]; then
  echo "ERROR: symlink in evidence path" >&2
  exit 4
fi

runuser -u "$TARGET_USER" -- install -d -m 0700 -- \
  "$ROOT" "$SAC_DIR" "$EVIDENCE_DIR" "$CAPTURE_DIR"
runuser -u "$TARGET_USER" -- test -w "$EVIDENCE_DIR"
runuser -u "$TARGET_USER" -- python3 "$AUTHORIZE" inspect "$EVIDENCE_DIR"

# Inspect the currently connected DUT without modifying its DTC memory.
# Pre-read also verifies the installed permanent agent is available.
if ! ip link show "$IFACE" >/dev/null 2>&1 ||
   ip link show "$IFACE" | head -1 | grep -q 'state UP'; then
  echo "ERROR: can0 unavailable or already in use" >&2
  exit 1
fi
PRE_VOLTAGE="$(runuser -u "$TARGET_USER" -- python3 \
  "$ROOT_DIR/scripts/ecu_bench.py" sac-parameters)"
echo "$PRE_VOLTAGE"
grep -qx 'ECU_BENCH_AGENT_STATUS=PASS' <<< "$PRE_VOLTAGE" ||
  { echo "ERROR: physical voltage preflight failed" >&2; exit 1; }
grep -qx 'CAN0_CLEANUP=DOWN' <<< "$PRE_VOLTAGE" ||
  { echo "ERROR: voltage preflight did not release CAN" >&2; exit 1; }

PRE_READ="$(runuser -u "$TARGET_USER" -- python3 \
  "$ROOT_DIR/scripts/ecu_bench.py" sac-dtc)"
echo "$PRE_READ"
grep -qx 'ECU_BENCH_AGENT_STATUS=PASS' <<< "$PRE_READ" ||
  { echo "ERROR: physical DTC preflight failed" >&2; exit 1; }
grep -qx 'CAN0_CLEANUP=DOWN' <<< "$PRE_READ" ||
  { echo "ERROR: DTC preflight did not release CAN" >&2; exit 1; }
if ! grep -Eq '^SAC_DTC_COUNT=[1-9][0-9]*$' <<< "$PRE_READ" ||
   ! grep -Eq '^SAC_DTC_AVAILABILITY_MASK=0x[0-9a-fA-F]+$' <<< "$PRE_READ"; then
  echo "SAC_CONTROLLED_RETEST=CANCELLED_INVALID_OR_EMPTY_DTC_INVENTORY; no 0x14" >&2
  exit 3
fi
if ip link show "$IFACE" | head -1 | grep -q 'state UP'; then
  echo "ERROR: CAN unexpectedly in use after agent preflight" >&2
  exit 1
fi

# FIRST interactive approval. Durable one-shot ticket is consumed before
# the CAN adapter is made active, even if the process is interrupted later.
runuser -u "$TARGET_USER" -- python3 "$AUTHORIZE" arm "$EVIDENCE_DIR"

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
PREFIX="$CAPTURE_DIR/sac-clear-retest-$STAMP-$$"
TRACE="$PREFIX.candump"
CAP_ERR="$PREFIX.candump.stderr"
CONSOLE="$PREFIX.console.txt"
CAN_STATS="$PREFIX.can-state.txt"
SUMMARY="$PREFIX.analysis.txt"
POST_READ="$PREFIX.post-read.txt"
CAPTURE_PID=""

cleanup() {
  local rc=$?
  trap - EXIT
  if [[ -n "$CAPTURE_PID" ]]; then
    kill -INT "$CAPTURE_PID" 2>/dev/null || true
    wait "$CAPTURE_PID" 2>/dev/null || true
  fi
  ip link set "$IFACE" down >/dev/null 2>&1 || true
  for file in "$TRACE" "$CAP_ERR" "$CONSOLE" "$CAN_STATS" "$SUMMARY" "$POST_READ"; do
    if [[ -f "$file" ]]; then
      chmod 0600 "$file" || true
      chown "$TARGET_USER" "$file" || true
    fi
  done
  echo "SAC_CONTROLLED_RETEST_CAN_CLEANUP=DOWN"
  return "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

ip link set "$IFACE" down
ip link set "$IFACE" type can bitrate 250000 fd off listen-only off
ip link set "$IFACE" up
ip -details -statistics link show "$IFACE" > "$CAN_STATS"
echo "SAC_CONTROLLED_RETEST_CAN_ACTIVE=YES"

# Important: candump started only AFTER can0 UP. Starting it on a down link
# exits with ENETDOWN. It must be active BEFORE the diagnostic program.
stdbuf -oL candump -L -t a \
  'can0,18DA30F9:1FFFFFFF,18DAF930:1FFFFFFF' \
  > "$TRACE" 2> "$CAP_ERR" &
CAPTURE_PID=$!
sleep 0.25
if ! kill -0 "$CAPTURE_PID" 2>/dev/null; then
  echo "ERROR: CAN trace recorder did not start; 0x14 NOT SENT" >&2
  exit 1
fi
echo "SAC_CONTROLLED_RETEST_TRACE=$TRACE"

# SECOND independent operator confirmation happens INSIDE the executable,
# after its own 19 02 FF read and durable 0600 backup. 'script' retains an
# interactive pseudo-TTY, records the entire console and preserves exit status.
printf -v PROBE_CMD '%q %q %q %q' \
  "$PROBE" "$IFACE" clear-dtc "$EVIDENCE_DIR"
set +e
runuser -u "$TARGET_USER" -- script -q -e -f -c "$PROBE_CMD" "$CONSOLE"
PROBE_RC=$?
set -e

# A possible late 0x54 / 7F 14 xx can arrive after the application's P2
# deadline. Receive-only for a bounded period: NEVER retransmit 0x14.
echo "SAC_CONTROLLED_RETEST_PASSIVE_WINDOW=8_SECONDS_NO_TX"
sleep 8
ip -details -statistics link show "$IFACE" >> "$CAN_STATS" || true
# Stop the sniffer BEFORE CAN DOWN to avoid a spurious ENETDOWN log error.
kill -INT "$CAPTURE_PID" 2>/dev/null || true
wait "$CAPTURE_PID" 2>/dev/null || true
CAPTURE_PID=""
ip link set "$IFACE" down

python3 "$ANALYZE" "$TRACE" > "$SUMMARY" || true
cat "$SUMMARY"
sha256sum "$TRACE" "$CONSOLE" "$CAN_STATS" >> "$SUMMARY"

# If the clear result is uncertain, an additional fresh READ-ONLY agent
# query records the state after the passive window. Never retry clear.
if grep -q 'SAC_DTC_CLEAR_REQUEST=START' "$CONSOLE"; then
  echo "SAC_CONTROLLED_RETEST_POST_READ=START_READ_ONLY"
  set +e
  runuser -u "$TARGET_USER" -- python3 \
    "$ROOT_DIR/scripts/ecu_bench.py" sac-dtc > "$POST_READ" 2>&1
  POST_RC=$?
  set -e
  cat "$POST_READ"
  echo "SAC_CONTROLLED_RETEST_POST_READ_RC=$POST_RC"
else
  echo "SAC_CONTROLLED_RETEST_POST_READ=SKIPPED_NO_CLEAR_START"
fi
if [[ "$PROBE_RC" -ne 0 ]]; then
  if grep -q 'SAC_DTC_CLEAR_UDS_ACK=YES' "$CONSOLE"; then
    echo "SAC_CONTROLLED_RETEST_RESULT=CLEAR_ACKNOWLEDGED_POST_READ_INCOMPLETE probe_exit=$PROBE_RC"
  else
    echo "SAC_CONTROLLED_RETEST_RESULT=CLEAR_ACK_UNCONFIRMED probe_exit=$PROBE_RC"
  fi
  echo "SAC_CONTROLLED_RETEST_NO_AUTOMATIC_RETRY=YES"
  exit "$PROBE_RC"
fi
if ! grep -q 'SAC_DTC_CLEAR_PHYSICAL=ACKNOWLEDGED_AND_RECHECKED' "$CONSOLE"; then
  echo "SAC_CONTROLLED_RETEST_RESULT=UNCONFIRMED_NO_ACK"
  exit 5
fi
if ! grep -qx 'SAC_TRACE_CLEAR_REQUESTS_OBSERVED=1' "$SUMMARY" ||
   ! grep -qx 'SAC_TRACE_CLEAR_ACKS_OBSERVED=1' "$SUMMARY" ||
   ! grep -Eq '^SAC_TRACE_POST_CLEAR_READ_RESPONSES=[1-9][0-9]*$' "$SUMMARY" ||
   ! grep -qx 'SAC_TRACE_PARSE_ERRORS=0' "$SUMMARY"; then
  echo "SAC_CONTROLLED_RETEST_RESULT=ACKNOWLEDGED_BUT_TRACE_INCOMPLETE"
  exit 6
fi
echo "SAC_CONTROLLED_RETEST_RESULT=ACKNOWLEDGED_AND_RECHECKED_WITH_TRACE"
echo "SAC_CONTROLLED_RETEST_TRACE_SHA256=$(sha256sum "$TRACE" | cut -d' ' -f1)"
echo "SAC_CONTROLLED_RETEST_REPORT=$SUMMARY"
