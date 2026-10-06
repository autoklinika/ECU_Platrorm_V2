#!/usr/bin/env bash
set -euo pipefail

export PATH="$HOME/.local/bin:/usr/local/bin:/usr/bin:/bin"

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || true)"
[[ -n "$REPO_ROOT" ]] || { echo "FAIL: run from ECU Platform repository" >&2; exit 2; }

mkdir -p "$HOME/.config/ecu-platform" "$HOME/ecu-agent-state" "$HOME/ecu-agent-worktrees"
chmod 700 "$HOME/.config/ecu-platform" "$HOME/ecu-agent-state" "$HOME/ecu-agent-worktrees"

echo "[ECU Platform] Autopilot readiness"
echo

missing=0
for cmd in git gh codex python3 tmux flock cmake ctest; do
  if command -v "$cmd" >/dev/null; then
    printf '%-10s %s\n' "$cmd" "$(command -v "$cmd")"
  else
    printf '%-10s MISSING\n' "$cmd"
    missing=1
  fi
done

echo
if gh auth status >/dev/null 2>&1; then
  echo "GITHUB_AUTH=PASS"
else
  echo "GITHUB_AUTH=REQUIRED"
fi

if codex login status >/dev/null 2>&1; then
  echo "CODEX_AUTH=PASS"
else
  echo "CODEX_AUTH=REQUIRED"
fi

ENV_FILE="$HOME/.config/ecu-platform/autopilot.env"
if [[ -f "$ENV_FILE" ]]; then
  if ECU_AUTOPILOT_ENV="$ENV_FILE" python3 "$REPO_ROOT/deploy/autopilot/notify_telegram.py" INFO bootstrap "ECU Platform autopilot readiness check." >/dev/null 2>&1; then
    echo "TELEGRAM_NOTIFY=PASS"
  else
    echo "TELEGRAM_NOTIFY=FAIL_OPTIONAL"
  fi
else
  echo "TELEGRAM_NOTIFY=DISABLED_OPTIONAL"
  cat <<EOF

Create $ENV_FILE with mode 600:
TELEGRAM_BOT_TOKEN=<token>
ECU_AUTOPILOT_TELEGRAM_CHAT_ID=<numeric-chat-id>
EOF
fi

echo
echo "TASK_STATE=$HOME/ecu-agent-state"
echo "TASK_WORKTREES=$HOME/ecu-agent-worktrees"

if ((missing)); then
  echo "ECU_AUTOPILOT_READY=NO"
  exit 3
fi

if gh auth status >/dev/null 2>&1   && codex login status >/dev/null 2>&1; then
  echo "ECU_AUTOPILOT_READY=YES"
else
  echo "ECU_AUTOPILOT_READY=NO"
fi
