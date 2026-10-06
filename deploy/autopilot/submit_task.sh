#!/usr/bin/env bash
set -Eeuo pipefail

export PATH="$HOME/.local/bin:/usr/local/bin:/usr/bin:/bin"

usage() {
  echo "usage: $0 <task-id> <base-branch> <spec-file> [--auto-merge]" >&2
}

TASK_ID="${1:-}"
BASE_BRANCH="${2:-}"
SPEC_FILE="${3:-}"
AUTO_MERGE=0
[[ "${4:-}" == "--auto-merge" ]] && AUTO_MERGE=1
[[ -n "$TASK_ID" && -n "$BASE_BRANCH" && -n "$SPEC_FILE" ]] || { usage; exit 2; }
[[ "$TASK_ID" =~ ^[a-z0-9][a-z0-9._-]{1,63}$ ]] || {
  echo "FAIL: task-id must match [a-z0-9][a-z0-9._-]{1,63}" >&2
  exit 2
}
[[ -f "$SPEC_FILE" ]] || { echo "FAIL: spec file not found: $SPEC_FILE" >&2; exit 2; }

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || true)"
[[ -n "$REPO_ROOT" ]] || { echo "FAIL: run from ECU Platform repository" >&2; exit 2; }

for cmd in git gh codex python3 tmux flock cmake ctest; do
  command -v "$cmd" >/dev/null || { echo "FAIL: missing command: $cmd" >&2; exit 3; }
done

gh auth status >/dev/null 2>&1 || {
  echo "FAIL: GitHub CLI is not authenticated. Run: gh auth login" >&2
  exit 4
}
codex login status >/dev/null 2>&1 || {
  echo "FAIL: Codex CLI is not authenticated. Run: codex login --device-auth" >&2
  exit 5
}

ENV_FILE="$HOME/.config/ecu-platform/autopilot.env"
[[ -f "$ENV_FILE" ]] || {
  echo "FAIL: Telegram autopilot env missing: $ENV_FILE" >&2
  exit 6
}

NOTIFY="$REPO_ROOT/deploy/autopilot/notify_telegram.py"
python3 "$NOTIFY" INFO "bootstrap" "ECU Platform autopilot notification channel test: PASS." >/dev/null || {
  echo "FAIL: Telegram notification test failed" >&2
  exit 6
}

git -C "$REPO_ROOT" ls-remote --exit-code origin "refs/heads/$BASE_BRANCH" >/dev/null || {
  echo "FAIL: remote base branch does not exist: $BASE_BRANCH" >&2
  exit 7
}

STATE_ROOT="$HOME/ecu-agent-state"
WORKTREE_ROOT="$HOME/ecu-agent-worktrees"
STATE_DIR="$STATE_ROOT/$TASK_ID"
SESSION="ecu-$TASK_ID"
mkdir -p "$STATE_ROOT" "$WORKTREE_ROOT"
chmod 700 "$STATE_ROOT" "$WORKTREE_ROOT"

[[ ! -e "$STATE_DIR" ]] || {
  echo "FAIL: task state already exists: $STATE_DIR" >&2
  exit 8
}
tmux has-session -t "$SESSION" 2>/dev/null && {
  echo "FAIL: tmux session already exists: $SESSION" >&2
  exit 8
}

mkdir -p "$STATE_DIR/logs"
chmod 700 "$STATE_DIR"
cp "$SPEC_FILE" "$STATE_DIR/spec.md"
chmod 600 "$STATE_DIR/spec.md"
{
  printf 'TASK_ID=%s\n' "$TASK_ID"
  printf 'BASE_BRANCH=%s\n' "$BASE_BRANCH"
  printf 'AUTO_MERGE=%s\n' "$AUTO_MERGE"
  printf 'CREATED_AT=%s\n' "$(date -Is)"
} > "$STATE_DIR/task.env"
printf 'QUEUED %s\n' "$(date -Is)" > "$STATE_DIR/status"

RUNNER="$REPO_ROOT/deploy/autopilot/run_task.sh"
launch="export PATH='$HOME/.local/bin:/usr/local/bin:/usr/bin:/bin'; export ECU_AUTOPILOT_TASK_ID='$TASK_ID'; export ECU_AUTOPILOT_BASE_BRANCH='$BASE_BRANCH'; export ECU_AUTOPILOT_AUTO_MERGE='$AUTO_MERGE'; export ECU_AUTOPILOT_REPO_ROOT='$REPO_ROOT'; export ECU_AUTOPILOT_STATE_ROOT='$STATE_ROOT'; export ECU_AUTOPILOT_WORKTREE_ROOT='$WORKTREE_ROOT'; export ECU_AUTOPILOT_ENV='$ENV_FILE'; exec bash '$RUNNER' > '$STATE_DIR/console.log' 2>&1"

tmux new-session -d -s "$SESSION" "$launch"
sleep 1

if ! tmux has-session -t "$SESSION" 2>/dev/null; then
  echo "FAIL: task session did not stay alive; inspect $STATE_DIR/console.log" >&2
  exit 9
fi

echo "STARTED task=$TASK_ID session=$SESSION state=$STATE_DIR base=$BASE_BRANCH auto_merge=$AUTO_MERGE"
