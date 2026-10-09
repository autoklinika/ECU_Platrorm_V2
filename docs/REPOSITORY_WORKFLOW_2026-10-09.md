# ECU Platform V2 — Repository & Workflow Baseline
_Date: 2026-10-09. Status: candidate awaiting review; does not change production main._

## Why this baseline exists

ECU Platform V2 is a laboratory tool for TRUCK / AGRI / OHV, not a
universal diagnostic tester. C++ Core V2 must remain DUT-neutral and
portable. Backend/DUT specifics belong in Bench Runtime / DUT Profile /
Application Layer; WebGUI remains a separate untrusted client.

The ongoing work has accumulated **50 remote branches, 13 open draft PRs,
and 11 clean local checkouts** (9 linked worktrees plus 2 separate API
Git checkouts). The Git objects and files are not damaged. The bottleneck
is finding the **active source** and keeping multiple stacked draft PRs
in sync.

This is a **non-destructive housekeeping release**. Existing branches,
PRs, operator evidence and worktrees are deliberately retained. No
automatic merges, rebases, Git garbage collection, resets, forced pushes
or deletions are performed.

## Canonical branch / PR streams

At the audit moment (2026-10-09, CM5):

| Workstream | Draft PR chain, base → tip | Known status |
|---|---|---|
| Application API | main → **#20 → #24** | API V1 base and physical SAC parameter snapshot implemented in candidate; review still pending |
| WebGUI / kiosk / SAC connect | main → **#19 → #21 → #22 → #23 → #25 → #26** | Physical GUI connection and bitrate negotiation tested at 500k; application/prototype candidate, not merged to main |
| DAF SAC Application / DTC lifecycle | dut-profile/proof-profiles → **#13 → #14 → #15 → #16 → #17** | Bench Agent/DTC read, gated clear operation and recovery candidates; destructive completion **not proven** |

Other branch families (Core V2, J1939, ISOBUS, platform, audits) remain
preserved as architectural baselines or historical development evidence.
A branch not in the table is **not** automatically obsolete.

**Production main is only changed after explicit owner approval for an
identified PR/commit set.** Do not confuse a clean worktree, a green CI
run, or a working CM5 deployment with acceptance for a main merge.

## What is actually finished / unfinished on DAF SAC

### A. Real parameter readings — OPEN

Confirmed physically on the 500 kbit/s SAC:

- UDS identity works (VIN DID F190 unprogrammed `17×FF`, software F188
  `2027746`, hardware F192 `K127968`);
- FE96 supply and ignition voltages produced **28.0 V / 28.0 V** in
  one completed read on 2026-10-09;
- J1939 PGN FEAE was observed, but both pressure values were **UNAVAILABLE**.
  This must render as missing values, not zeros;
- read-only published readout passed and older DTC readout was preserved;
- GUI automatic 250 → 500 kbit/s negotiation was operator-accepted for
  the connected 500k SAC. Dedicated 250k physical DUT acceptance remains
  separate from unit tests.

**Not yet closed:** agree what "real-time" means for the laboratory
parameters (operator-requested completed snapshots vs periodic acquisition),
verify refresh lifecycle at the GUI/API boundary, independently prove the
availability/meaning of pressure channels when corresponding hardware is
connected, and verify timeout/error/reconnect behavior across repeated
sessions. Archived `/api/v1/readouts/daf-sac/parameters/latest` is not
a continuous live CAN stream.

**Acceptance checklist:** actual DUT measurements; profile/bitrate
association; measurement timestamp and expiration; no stale or
cross-DUT display; no fabricated zero pressure; CAN DOWN after operation;
no direct CAN from GUI.

### B. DTC clear — OPEN, DESTRUCTIVE GATE

Prior controlled one-time physical attempt received positive UDS
`0x54` for `14 FF FF FF`. However the immediate post-clear read timed
out and an independent subsequent read still reported **12 DTC**. Thus
**positive service ACK is not evidence that DTC history was cleared**.
A previous `UNKNOWN` archive and the consumed one-time retest ticket
must remain intact.

The permanently installed Bench Agent intentionally allows reads only,
not `sac.clear_dtc`. **Do not add a DTC-clear button to the kiosk or
run a new `0x14` from CI, release smoke, scheduled scripts or API.**

Completion requires a separately approved physical operator protocol,
explicit re-authorization (not bypassing a spent ticket), pre/post evidence,
a clear decision on which errors should disappear and which active faults
may immediately return, correct status classification, and independent
verification of read-only `19 02 FF`. Full history is in
[DAF_SAC_CONTROLLED_RETEST_2026-10-08.md](DAF_SAC_CONTROLLED_RETEST_2026-10-08.md).
No CORE rewrite without an actual architecture defect.

## One-command start of a work session

The portable, dependency-light tool
`scripts/ecu_repo_doctor.py` is intentionally read-only.

From **any checkout carrying this script** (or via its absolute path):

```bash
# All local checkouts (including separate API clones), open PR trains,
# deployed CM5 status and CAN UP/DOWN — observation only:
python3 scripts/ecu_repo_doctor.py status --github --siblings --cm5

# Script-friendly JSON for session notes and reviews:
python3 scripts/ecu_repo_doctor.py status --github --siblings --cm5 --json

# Quick OFFLINE checks — no CAN, no sudo, no DTC clear:
python3 scripts/ecu_repo_doctor.py check --scope repo
python3 scripts/ecu_repo_doctor.py check --scope webgui
python3 scripts/ecu_repo_doctor.py check --scope api
python3 scripts/ecu_repo_doctor.py check --scope sac

# Verify cleanliness; GitHub is optional and not needed for local use:
python3 scripts/ecu_repo_doctor.py status --siblings --strict
```

The doctor checks Git state by itself, optionally calls `gh pr list`
read-only for current dependency chains, reads CM5 systemd activity and
the CAN link UP/DOWN state, and runs explicitly allowlisted offline test
suites. **No fetch/reset/checkout/merge/delete/install/flash/CAN transmit
command is implemented.** The script and unit tests run on Linux/Windows;
only `--cm5` is host-specific and skips elsewhere. `gh` is optional.

### Recommended working checkouts (do not delete others yet)

| Focus | Existing checkout | Use |
|---|---|---|
| WebGUI / kiosk / 250-500k connection | `~/ECU_WEBGUI_PARAMS_V1` | PR #26 candidate and on-device GUI work |
| API readout contracts / SAC snapshot | `~/ECU_API_PARAMS_V1` | PR #24 candidate |
| DUT/Bench proof review | `~/ECU_Platrorm_V2` | Historical Stage 4.3 physical evidence, not automatically production |
| Production release snapshot | `origin/main` | Read-only reference until merge approval |

The repository workflow tool can be hosted in one dedicated workflow
checkout (for example `~/ECU_V2_WORKFLOW`), without changing application
worktrees. Use `--root /home/ecu/ECU_WEBGUI_PARAMS_V1` or the API checkout
when running checks from the operations checkout.

Other clean historical worktrees are candidates for **later
operator-approved local workspace retirement**. Removing a worktree is
not the same as deleting its remote branch, but ignored build products,
untracked evidence and local-only files must be audited before any removal.

## Faster sequence for changes

1. Start with `ecu_repo_doctor status` and confirm one active branch and
   clean checkout. Read the current task's evidence document.
2. Choose **one workstream**: WebGUI, Application API/parameters or gated
   DTC/Bench; do not mix unrelated functionality into the same PR.
3. Use a short topic branch and a draft PR with the existing parent PR
   as base where appropriate. Avoid creating a new worktree for a
   one-file change unless another branch is already checked out there.
4. Run `ecu_repo_doctor check --scope ...`, then the relevant
   CMake/CTest and Linux/Windows CI. Simulations never imply physical PASS.
5. Deploy via narrow, reversible operator-only script with a backup and
   smoke. **CAN physical tests require a separate deliberate action.**
6. Record verified DUT facts, commit SHA and outstanding gaps in the
   task's documentation. Mark a PR ready only after gates pass.
7. Prepare an **explicit consolidated release proposal** with a scoped
   diff and multi-platform regression for review. Do **not** merge any
   chain into `main` without owner approval.

## Consolidation policy (proposal, no action yet)

- Keep the three PR trains and their evidence until the two SAC
  functional gaps are resolved.
- Once a release candidate is stable, prepare a **temporary integration
  candidate branch** with the accepted workstreams, run cross-platform
  full CI and a CM5 read-only hardware smoke, and present an exact merge
  plan. Integration into such a branch is not permission to move `main`.
- Only after explicit authorization: merge the identified release,
  then review obsolete PRs and redundant local worktrees for archival
  or cleanup. Do not use `git branch -D`, `git worktree remove --force`,
  `git push --delete`, `git gc --prune` or `git reset --hard` as
  an automated "cleanup".

## Project boundaries

Do not introduce dependencies on the separate AI Platform or other
projects. The legacy ECU repository is a read-only implementation/
styling reference. WebGUI remains unprivileged and has no direct CAN
access; the application's read-only API and controlled backend own
all DUT operations. Multiplatform CORE V2 must not depend on CM5,
systemd, Qt, Linux sockets or HTTP transport.
