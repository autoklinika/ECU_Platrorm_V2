#!/usr/bin/env bash
set -Eeuo pipefail

# ONCE per machine (or when explicitly upgrading root-owned capabilities).
# Run locally in an operator terminal: sudo ./scripts/install_ecu_bench_agent.sh
# No secrets, password cache, NOPASSWD sudoers, setcap(ip), or root shell.
if [[ "$EUID" -ne 0 || ! -t 0 || ! -t 1 ]]; then
  echo "ERROR: run once locally with 'sudo' in an interactive terminal"
  exit 2
fi

OPERATOR="${SUDO_USER:-}"
if [[ "$OPERATOR" != ecu ]]; then
  echo "ERROR: this deployment is pinned to the local operator account 'ecu'"
  exit 2
fi
if [[ "$(id -u "$OPERATOR")" -eq 0 ]]; then
  echo "ERROR: operator cannot be root"
  exit 2
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
SRC="$ROOT/deploy/ecu_bench_agent/agent_server.py"
TEMPLATE="$ROOT/deploy/ecu_bench_agent/ecu-platform-v2-bench-agent.service.in"
PROBE="$ROOT/build/daf-sac-app-linux/tests/ecu_daf_sac_stage42_read_probe"
TARGET="/usr/local/libexec/ecu-platform-v2"
UNIT="/etc/systemd/system/ecu-platform-v2-bench-agent.service"

for file in "$SRC" "$TEMPLATE" "$PROBE"; do
  if [[ ! -f "$file" ]]; then
    echo "ERROR: required artifact missing: $file"
    exit 1
  fi
done
if [[ ! -x "$PROBE" ]]; then
  echo "ERROR: existing SAC read-only proof is not executable"
  exit 1
fi
if ! /usr/sbin/ip link show can0 >/dev/null 2>&1; then
  echo "ERROR: can0 is not present"
  exit 1
fi
if /usr/sbin/ip -o link show can0 | grep -qE '(<|,)UP(,|>)'; then
  echo "ERROR: can0 is UP. Do not replace the agent during an active CAN session"
  exit 1
fi

echo "[ECU V2] Permanent limited bench authorization"
echo "Operator: ecu"
echo "Only allowed remote operations: sac.read_dtc, sac.read_parameters, status"
echo "Unsafe services, DTC clear, flash, shell, other interfaces: NOT DELEGATED"
echo "Privileged agent/server and the executable probe are copied ROOT OWNED"
echo "One sudo password is entered locally during this installation only."
echo

# Stop every prior installed unit, including a crashing/restarting one.
# is-active alone would miss 'activating (auto-restart)'.
# This happens only after we verified CAN is DOWN.
if [[ -f "$UNIT" ]]; then
  systemctl stop ecu-platform-v2-bench-agent.service
fi

# Parent is root-owned and not writable by the operator, even when the
# project checkout or current CMake build tree is modified later.
install -d -o root -g root -m 0755 "$TARGET"
install -o root -g root -m 0755 "$SRC" "$TARGET/agent_server.py"
install -o root -g root -m 0755 "$PROBE" "$TARGET/sac-read-probe"

tmp="$(mktemp /run/ecu-v2-bench-agent-unit.XXXXXXXX)"
cleanup_tmp() { rm -f "$tmp"; }
trap cleanup_tmp EXIT
sed "s|@OPERATOR@|$OPERATOR|g" "$TEMPLATE" > "$tmp"
install -o root -g root -m 0644 "$tmp" "$UNIT"

systemctl daemon-reload
systemctl enable --now ecu-platform-v2-bench-agent.service
if ! systemctl is-active --quiet ecu-platform-v2-bench-agent.service; then
  echo "ERROR: permanent bench agent service did not start"
  systemctl stop ecu-platform-v2-bench-agent.service || true
  echo "Service stopped after failed startup; inspect journalctl -u ecu-platform-v2-bench-agent.service"
  exit 1
fi
for counter in 1 2 3 4 5 6 7 8 9 10; do
  [[ -S /run/ecu-platform-v2-bench/request.sock ]] && break
  sleep 1
done
if [[ ! -S /run/ecu-platform-v2-bench/request.sock ]]; then
  echo "ERROR: agent socket did not become ready"
  systemctl stop ecu-platform-v2-bench-agent.service || true
  echo "Service stopped after failed startup; inspect journalctl -u ecu-platform-v2-bench-agent.service"
  exit 1
fi
runuser -u "$OPERATOR" -- /usr/bin/python3 "$ROOT/scripts/ecu_bench.py" status
echo "ECU_BENCH_AGENT_INSTALLED=PASS"
echo "ECU_BENCH_AGENT_BOOT_PERSISTENCE=ENABLED"
echo
echo "Unprivileged command, available now and after reboot:"
echo "python3 $ROOT/scripts/ecu_bench.py status"
echo "python3 $ROOT/scripts/ecu_bench.py sac-dtc"
echo "python3 $ROOT/scripts/ecu_bench.py sac-parameters"
echo
echo "Revoke later (requires operator local sudo):"
echo "sudo systemctl disable --now ecu-platform-v2-bench-agent.service"
