#!/usr/bin/env bash
set -Eeuo pipefail

export PATH="$HOME/.local/bin:/usr/local/bin:/usr/bin:/bin"

TASK_ID="${ECU_AUTOPILOT_TASK_ID:?missing ECU_AUTOPILOT_TASK_ID}"
BASE_BRANCH="${ECU_AUTOPILOT_BASE_BRANCH:?missing ECU_AUTOPILOT_BASE_BRANCH}"
AUTO_MERGE="${ECU_AUTOPILOT_AUTO_MERGE:-0}"
REPO_ROOT="${ECU_AUTOPILOT_REPO_ROOT:-$HOME/ECU_Platrorm_V2}"
STATE_ROOT="${ECU_AUTOPILOT_STATE_ROOT:-$HOME/ecu-agent-state}"
WORKTREE_ROOT="${ECU_AUTOPILOT_WORKTREE_ROOT:-$HOME/ecu-agent-worktrees}"
STATE_DIR="$STATE_ROOT/$TASK_ID"
WORKTREE="$WORKTREE_ROOT/$TASK_ID"
SPEC="$STATE_DIR/spec.md"
BRANCH="agent/$TASK_ID"
LOG_DIR="$STATE_DIR/logs"
NOTIFY="$REPO_ROOT/deploy/autopilot/notify_telegram.py"
REPO_SLUG="autoklinika/ECU_Platrorm_V2"

mkdir -p "$STATE_DIR" "$LOG_DIR" "$WORKTREE_ROOT"

exec 9>"$STATE_ROOT/master.lock"
if ! flock -n 9; then
  printf 'BLOCKED %s\\n' "$(date -Is)" > "$STATE_DIR/status"
  printf 'another ECU autopilot task is already running\\n' > "$STATE_DIR/reason"
  ECU_AUTOPILOT_ENV="${ECU_AUTOPILOT_ENV:-$HOME/.config/ecu-platform/autopilot.env}" \\
    python3 "$NOTIFY" BLOCKED "$TASK_ID" "Another ECU autopilot task is already running." >/dev/null 2>&1 || true
  exit 2
fi

set_state() {
  printf '%s %s\n' "$1" "$(date -Is)" > "$STATE_DIR/status"
}

notify() {
  ECU_AUTOPILOT_ENV="${ECU_AUTOPILOT_ENV:-$HOME/.config/ecu-platform/autopilot.env}" \
    python3 "$NOTIFY" "$@" >/dev/null 2>&1 || true
}

blocked() {
  local why="$1"
  set_state BLOCKED
  printf '%s\n' "$why" > "$STATE_DIR/reason"
  notify BLOCKED "$TASK_ID" "$why"
}

unexpected_failure() {
  local rc=$?
  blocked "Supervisor task failed unexpectedly (rc=$rc). No merge was performed."
  exit "$rc"
}
trap unexpected_failure ERR

for cmd in git gh codex python3 cmake ctest; do
  command -v "$cmd" >/dev/null || {
    blocked "Missing required command: $cmd"
    exit 3
  }
done

gh auth status >/dev/null 2>&1 || {
  blocked "GitHub CLI is not authenticated."
  exit 4
}

codex login status >/dev/null 2>&1 || {
  blocked "Codex CLI is not authenticated."
  exit 5
}

[[ -f "$SPEC" ]] || {
  blocked "Task specification missing: $SPEC"
  exit 6
}

if [[ -e "$WORKTREE" ]]; then
  if git -C "$WORKTREE" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    [[ -z "$(git -C "$WORKTREE" status --porcelain)" ]] || {
      blocked "Existing worktree is dirty: $WORKTREE"
      exit 7
    }
    git -C "$REPO_ROOT" worktree remove "$WORKTREE" --force
  else
    blocked "Worktree path exists but is not a Git worktree: $WORKTREE"
    exit 7
  fi
fi

set_state PREFLIGHT
notify STARTED "$TASK_ID" "Task accepted. Preparing isolated worktree from $BASE_BRANCH."

git -C "$REPO_ROOT" fetch origin "$BASE_BRANCH" --prune
if git -C "$REPO_ROOT" show-ref --verify --quiet "refs/heads/$BRANCH"; then
  git -C "$REPO_ROOT" branch -D "$BRANCH"
fi
git -C "$REPO_ROOT" worktree add -b "$BRANCH" "$WORKTREE" "origin/$BASE_BRANCH"

cat > "$STATE_DIR/implement.prompt" <<EOF
You are the implementation agent for ECU Platform V2.

PRODUCT SCOPE IS AN INVARIANT:
- TRUCK / heavy-duty
- AGRI
- OHV
Passenger cars are out of scope.

Work only inside the current repository/worktree.
Do not commit, push, merge, create PRs, call gh, or modify external production systems.
The supervisor owns Git/GitHub actions.

Read and obey before changing code:
- docs/ZALOZENIA_ROBOCZE.md
- docs/CORE_ARCHITECTURE_AUDIT.md
- docs/CORE_STANDARDS_TRACEABILITY.md
- the task specification appended below

Engineering rules:
- standards-first and traceable,
- fail closed on uncertainty,
- preserve portability boundaries,
- no Linux/hardware dependencies inside portable Core,
- add tests for new behavior and negative/boundary cases,
- never claim standards conformance without evidence,
- keep changes scoped and reversible.

Run the appropriate local tests.
When the requested implementation is genuinely ready for independent review,
write exactly READY_FOR_REVIEW followed by a newline to:
.agent-result

TASK SPECIFICATION:
EOF
cat "$SPEC" >> "$STATE_DIR/implement.prompt"

set_state IMPLEMENT
(
  cd "$WORKTREE"
  codex --sandbox workspace-write --ask-for-approval never exec < "$STATE_DIR/implement.prompt"
) 2>&1 | tee "$LOG_DIR/codex-implement.log"

marker="$WORKTREE/.agent-result"
[[ -f "$marker" ]] || {
  blocked "Codex finished without READY_FOR_REVIEW marker."
  exit 10
}
[[ "$(tr -d '\r\n' < "$marker")" == "READY_FOR_REVIEW" ]] || {
  blocked "Invalid Codex result marker."
  exit 10
}
rm -f "$marker"

set_state DEV_GATE
notify CHECKPOINT "$TASK_ID" "Implementation finished. Running repository gates and full Core validation."

git -C "$WORKTREE" diff --check
(
  cd "$WORKTREE"
  bash scripts/check_core_standards.sh
  bash scripts/check_core_architecture.sh
  bash scripts/check_core_portability.sh
  bash scripts/validate_core_hardening.sh
) 2>&1 | tee "$LOG_DIR/dev-gate.log"

cat > "$STATE_DIR/review.prompt" <<EOF
You are the independent read-only reviewer for ECU Platform V2 task: $TASK_ID.

Review the current worktree diff against origin/$BASE_BRANCH.
The target product scope is only TRUCK, AGRI and OHV.

Read:
- docs/ZALOZENIA_ROBOCZE.md
- docs/CORE_ARCHITECTURE_AUDIT.md
- docs/CORE_STANDARDS_TRACEABILITY.md
- $SPEC

Reject if:
- passenger-car-only scope leaks into the architecture,
- J1939 / ISOBUS / heavy-duty priorities are weakened,
- a standards claim lacks evidence,
- Core portability or layering is violated,
- tests do not cover changed behavior and important negative/boundary cases,
- the implementation knowingly leaves a regression,
- secrets or private runtime data enter the repository.

You are read-only. Do not modify files.

Your final line MUST be exactly one of:
AUTOPILOT_REVIEW=PASS
AUTOPILOT_REVIEW=BLOCKED
EOF

set_state REVIEW
(
  cd "$WORKTREE"
  codex --sandbox read-only --ask-for-approval never exec < "$STATE_DIR/review.prompt"
) 2>&1 | tee "$LOG_DIR/codex-review.log"

grep -qx 'AUTOPILOT_REVIEW=PASS' "$LOG_DIR/codex-review.log" || {
  blocked "Independent Codex review did not return PASS."
  exit 11
}

set_state COMMIT
git -C "$WORKTREE" add -A
git -C "$WORKTREE" diff --cached --quiet && {
  blocked "Task produced no repository changes."
  exit 12
}

git -C "$WORKTREE" -c user.name="ECU Platform Autopilot" \
  -c user.email="ecu-platform-autopilot@users.noreply.github.com" \
  commit -m "autopilot($TASK_ID): implement validated task"

set_state PUSH
git -C "$WORKTREE" push -u origin "$BRANCH"
head_sha="$(git -C "$WORKTREE" rev-parse HEAD)"

set_state PR
pr_url="$(cd "$WORKTREE" && gh pr create \
  --repo "$REPO_SLUG" \
  --base "$BASE_BRANCH" \
  --head "$BRANCH" \
  --title "Autopilot: $TASK_ID" \
  --body "Autonomous ECU Platform task. Scope: TRUCK / AGRI / OHV. Local Core hardening gate and independent Codex review passed. GitHub CI is required before merge.")"
printf '%s\n' "$pr_url" > "$STATE_DIR/pr.url"
pr_number="$(printf '%s' "$pr_url" | sed -nE 's#.*/pull/([0-9]+).*#\1#p')"
[[ -n "$pr_number" ]] || {
  blocked "Could not determine pull request number."
  exit 13
}

wait_ci() {
  local sha="$1"
  local run_id=""
  local row=""
  local status=""
  local conclusion=""
  local observed_sha=""

  for _ in $(seq 1 120); do
    row="$(gh run list --repo "$REPO_SLUG" --workflow "ECU Platform CI" \
      --limit 50 --json databaseId,headSha,status,conclusion \
      --jq ".[] | select(.headSha == \"$sha\") | .databaseId" 2>/dev/null | head -n1 || true)"
    if [[ -n "$row" ]]; then
      run_id="$row"
      break
    fi
    sleep 5
  done

  [[ -n "$run_id" ]] || return 20
  printf '%s\n' "$run_id" > "$STATE_DIR/ci.run_id"

  for _ in $(seq 1 240); do
    read -r status conclusion observed_sha <<<"$(gh run view "$run_id" --repo "$REPO_SLUG" \
      --json status,conclusion,headSha \
      --jq '[.status,(.conclusion // "-"),.headSha] | join(" ")' 2>/dev/null || true)"
    [[ -z "$observed_sha" || "$observed_sha" == "$sha" ]] || return 21
    if [[ "$status" == "completed" ]]; then
      [[ "$conclusion" == "success" ]] && return 0
      return 22
    fi
    sleep 5
  done
  return 23
}

set_state CI
notify CHECKPOINT "$TASK_ID" "PR created. Waiting for ECU Platform CI."
if ! wait_ci "$head_sha"; then
  rc=$?
  blocked "GitHub CI failed or timed out (rc=$rc). PR left open; no merge performed."
  exit "$rc"
fi

if [[ "$AUTO_MERGE" == "1" && "$BASE_BRANCH" != "main" ]]; then
  set_state MERGE
  (cd "$WORKTREE" && gh pr merge "$pr_number" --repo "$REPO_SLUG" --merge --delete-branch)
  git -C "$REPO_ROOT" fetch origin "$BASE_BRANCH" --prune
  set_state COMPLETE
  notify COMPLETE "$TASK_ID" "Implementation, local gates, independent review, PR and CI: PASS. Merged into $BASE_BRANCH."
else
  set_state READY_FOR_MERGE
  notify COMPLETE "$TASK_ID" "Implementation, local gates, independent review, PR and CI: PASS. PR is ready; merge was intentionally not automatic."
fi
