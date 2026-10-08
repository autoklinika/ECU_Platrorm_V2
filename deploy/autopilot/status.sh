#!/usr/bin/env bash
set -euo pipefail

STATE_ROOT="${ECU_AUTOPILOT_STATE_ROOT:-$HOME/ecu-agent-state}"
TASK_ID="${1:-}"

show_one() {
  local dir="$1"
  local id
  id="$(basename "$dir")"
  local status="MISSING"
  local pr="-"
  local reason="-"
  [[ -f "$dir/status" ]] && status="$(cat "$dir/status")"
  [[ -f "$dir/pr.url" ]] && pr="$(cat "$dir/pr.url")"
  [[ -f "$dir/reason" ]] && reason="$(tr '\n' ' ' < "$dir/reason")"
  printf 'TASK=%s\nSTATUS=%s\nPR=%s\nREASON=%s\n' "$id" "$status" "$pr" "$reason"
  if [[ -f "$dir/console.log" ]]; then
    echo '--- LAST LOG ---'
    tail -n 20 "$dir/console.log"
  fi
}

if [[ -n "$TASK_ID" ]]; then
  dir="$STATE_ROOT/$TASK_ID"
  [[ -d "$dir" ]] || { echo "TASK_NOT_FOUND=$TASK_ID"; exit 1; }
  show_one "$dir"
  exit 0
fi

if [[ ! -d "$STATE_ROOT" ]]; then
  echo "NO_TASKS"
  exit 0
fi

found=0
for dir in "$STATE_ROOT"/*; do
  [[ -d "$dir" ]] || continue
  found=1
  show_one "$dir"
  echo
done
((found)) || echo "NO_TASKS"
