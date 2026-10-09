# ECU Platform V2 — consolidated operational candidate (2026-10-09)

**Integration branch:** `integration/ecu-v2-operational-candidate-20261009`.
**Production base:** `main` at `29666b5659d5`. Production `main`
remains unchanged. This is an integration proposal, **not** authorization
to merge into production.

## Scope already included

The integration commit graph contains, unchanged and with full ancestry:

| Source | Tip | Features |
|---|---|---|
| `api/v1-sac-parameters-readout-20261008` | `ec1e2c3aa04e` | Portable C++ API V1, Linux read-only transport, authorized completed DTC/parameter snapshots |
| `webgui/sac-local-prototype-no-token-20261009` | `81a390641a18` | CM5 kiosk, WebGUI, 250/500 kbit/s automatic SAC identification, VIN/SW/HW flow, historical parameters, local credential proxy |
| `workflow/repository-hygiene-20261009` | `5ad5a0545979` | Repository doctor, portable offline tests, project workflow documentation |

Two merge conflicts were limited to `.gitignore`. The final ignore
file unifies both branches' build/cache restrictions and adds protection
for private trace/log formats. No CORE/Bench protocol code was changed
during integration; only merge commits and the candidate release record.

All original branches, private CAN evidence, local CM5 releases and DTC
archives were preserved. This branch was created in a separate worktree;
**no shell command wrote to production `main`.**

## Completed engineering / review cleanup

Historic PRs **#13, #14, #15, #16, #17** have heads that are already
reachable from production `main` (zero branch-only commits). They were
closed as redundant review requests on 2026-10-09; their source
branches and commits were **not** deleted. This statement does NOT
verify destructive DTC clearing.

The following 8 draft PRs are fully represented by this candidate and
can be closed as superseded review requests **only after the candidate
itself is published and passes integration gates**:

- WebGUI: #19 → #21 → #22 → #23 → #25 → #26
- API: #20 → #24
- Repo tooling #27 is also included.

The consolidated candidate is the sole review entrypoint for these
nonproduction lines. Closing a superseded PR does **not** merge it,
remove its branch, or mark functional gaps complete.

## Explicitly unfinished and excluded from acceptance claims

### 1. Physical SAC parameter readout

A real DAF SAC 500k hardware test returned positive read-only VIN/SW/HW,
FE96 permanent/ignition `28.0 V`, and correctly nullable FEAE pressure
channels. A completed snapshot was published and the old DTC readout
remained intact. GUI identification and automatic 250→500 fallback
were operator-accepted. However, the full repeatable **parameter
refresh/lifecycle** and physical pressure-channel evidence are not yet
accepted as finished. Historical readout data must remain labeled as
completed, never falsely live.

### 2. DTC clear

One physically authorized UDS `14 FF FF FF` obtained a positive `0x54`
acknowledgment; immediate post-clear verification was incomplete and
an independent subsequent read still contained 12 DTC. This does
**not** prove successful clearing. The one-time destructive retest
ticket is consumed. Do not reissue, auto-retry, add a kiosk clear
button, or give a permanent agent new privileges without a separately
approved plan. Existing conservative guards and evidence are preserved.

### 3. Hardware portability & remaining validation

250 kbit/s automatic connection is covered by offline tests but requires
a separate physically compatible SAC/DUT to claim 250k physical PASS.
CORE portability is covered by original CI; runtime-specific CM5 and
systemd adapters remain isolated from portable layers.

## Candidate gates

- Git ancestry for all three input tips — PASS.
- Integrated offline WebGUI/Node and API/Python plus repository doctor —
  PASS, with no physical CAN action.
- Full cross-platform Core/API/WebGUI/repository CI — tracked on the
  candidate PR, **must pass before review**.
- Operator-authorized CM5 deployment and post-release read-only smoke —
  **not performed by integration**; current kiosk release stays intact.
- Owner approval of **this concrete changeset** required before *any*
  production-main merge. Do not interpret PR closure or a green CI as
  automatic merge authorization.

## New working rules

Use `~/ECU_V2_WORKFLOW/scripts/ecu_repo_doctor.py` for quick read-only
status and offline tests. Work primarily from:
`~/ECU_WEBGUI_PARAMS_V1` (SAC GUI), `~/ECU_API_PARAMS_V1` (API),
and the integration candidate checkout only for cross-layer regression.
Do not create new stacked historical PRs for each small change.
Instead, feature-specific PRs target the appropriate candidate branch
until a consolidated release is reviewed. Do not remove additional
worktrees until their source and ignored files are audited.
