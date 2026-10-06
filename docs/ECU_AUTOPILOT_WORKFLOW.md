# ECU Platform V2 — autonomous server-side development workflow

Status: ACTIVE

## Goal

ECU Platform development tasks can continue on the `ecu` host after the operator
PC/browser is disconnected.

The workflow intentionally mirrors the proven AI Platform pattern:

1. task is submitted on the server,
2. a durable `tmux` session owns execution,
3. Codex implements only inside an isolated Git worktree,
4. repository gates and full Core validation run locally,
5. a second read-only Codex pass performs an independent review,
6. the supervisor owns commit/push/PR actions,
7. GitHub Actions is the remote CI gate,
8. Telegram reports only meaningful milestones or blockers,
9. task state/logs remain on disk and can be queried later.

## Safety boundaries

- Product scope is invariant: TRUCK + AGRI + OHV.
- Passenger-car-only scope is rejected.
- The task agent may not commit, push, merge, call GitHub, or modify external
  production systems.
- The supervisor owns Git and GitHub mutations.
- Tasks are serialized by a global `flock`.
- A task may auto-merge only into a non-`main` development branch and only
  when explicitly submitted with `--auto-merge`.
- `main` is never auto-merged by this v1 supervisor.
- Standards PASS is never inferred from ordinary unit tests.
- Missing auth, failed tests, failed independent review, or failed CI stops the
  task fail-closed.

## Server directories

Private runtime state is not stored in Git:

- `~/.config/ecu-platform/autopilot.env`
- `~/ecu-agent-state/<task-id>/`
- `~/ecu-agent-worktrees/<task-id>/`

Each task state directory contains the copied specification, current status,
logs, PR URL and blocker reason when applicable.

## One-time readiness

From the repository:

```bash
export PATH="$HOME/.local/bin:$PATH"
bash deploy/autopilot/bootstrap.sh
```

Required one-time authentication:

```bash
gh auth login
codex login --device-auth
```

Telegram notifier private environment:

```text
TELEGRAM_BOT_TOKEN=<secret>
ECU_AUTOPILOT_TELEGRAM_CHAT_ID=<numeric-chat-id>
```

The file must be stored at:

```text
~/.config/ecu-platform/autopilot.env
```

with permissions `0600`.

## Submit a task

Create a Markdown task specification outside the repository or in a temporary
operator directory, then run:

```bash
bash deploy/autopilot/submit_task.sh \
  core-v2-baseline \
  core-hardening/v1-foundation \
  /path/to/core-v2-baseline.md
```

To allow automatic merge after local gates, independent review and GitHub CI:

```bash
bash deploy/autopilot/submit_task.sh \
  core-v2-baseline \
  core-hardening/v1-foundation \
  /path/to/core-v2-baseline.md \
  --auto-merge
```

Auto-merge is refused for `main`.

## Check status

All tasks:

```bash
bash deploy/autopilot/status.sh
```

One task:

```bash
bash deploy/autopilot/status.sh core-v2-baseline
```

Expected states include:

- `QUEUED`
- `PREFLIGHT`
- `IMPLEMENT`
- `DEV_GATE`
- `REVIEW`
- `COMMIT`
- `PUSH`
- `PR`
- `CI`
- `MERGE`
- `READY_FOR_MERGE`
- `COMPLETE`
- `BLOCKED`

## Telegram policy

The notifier is one-way and uses only Telegram `sendMessage`. It never calls
`getUpdates`, so it does not compete with another Telegram receiver.

Events:

- STARTED
- CHECKPOINT
- COMPLETE
- BLOCKED
- ROLLBACK
- INFO

No tokens, credentials, prompts, private logs or secret environment values may
be included in notifications.

## GitHub CI

Workflow: `ECU Platform CI`

The CI gate runs:

- standards baseline gate,
- Core architecture gate,
- Core portability gate,
- full Debug/Release Core hardening validation,
- sanitizer coverage included by the hardening validator.

Hardware-in-the-loop validation is intentionally separate and will be added when
hardware mutation/cutover stages are introduced.

## Future extension

When ECU Platform gains a production runtime and mutating hardware workflows,
add the AI Platform-style privileged bridge and explicit sequence:

preflight -> build/install -> cutover -> live smoke -> rollback ->
rollback smoke -> reactivate -> final smoke -> evidence.

Until then, the autonomous runner is development-only and cannot modify
production or vehicle hardware.
