# Issue #29 — final screen-scoped SAC session pipeline

Date: 2026-10-09. This supersedes the previous implementation which re-identified SAC on every refresh.

## Required application flow

1. **Connect SAC** — one operator-triggered identification using VIN (F190), SW (F188), HW (F192). Automatic bitrate discovery 250 → 500 kbit/s is performed **once per connection**. The privileged SAC adapter retains the accepted CAN bitrate and DUT profile as a bounded, server-owned session. Connecting does **not** perform an FE96 read.
2. **Parameters screen** — only when the screen is visible, WebGUI invokes fixed, authenticated `POST /kiosk/v1/bench/daf-sac/parameters/read`. The adapter uses the stored bitrate/profile and invokes the existing native read-only FE96 operation and passive FEAE monitor. **No identity UDS service is invoked during a parameter cycle.** The immutable parameter record is read through the existing API endpoint. The next operation starts 1200ms after the preceding one completes.
3. **DTC screen** — does not call the parameters endpoint or request identity. The current DTC API is a read-only historical snapshot; future Issue #30 work remains separate.
4. **Other screens** — no parameter/identity/DTC read is scheduled unless the screen owns that operation.

## Safety and integrity
- WebGUI remains a client. The SAC adapter is the only existing privileged access path; no generic CAN/UDS endpoint is added.
- No CORE V2, Bench Runtime, DUT Profile or hardware decoder changes.
- One native operation at a time, each with its own timeout and CAN-down cleanup; queued read requests never overlap.
- Backend session expires after 15 minutes of inactivity, or immediately on a failed reconnect/service restart. Expired session requires explicit new connection; **not an automatic identity retry**.
- The operation returns only profile, bitrate, outcome, capture timestamp and completed generation — never VIN/SW/HW. The UI matches this against the original connected profile and the exact API snapshot; old or invalid observations are not displayed as current.
- Pressures marked unavailable in FEAE are `null`, never synthetic zero.
- Leaving the screen/hidden document prevents new requests. An in-flight native read finishes its safe CAN cleanup without its late response being rendered.
- Authentication, kiosk origin/proxy allowlist, DTC data and production main remain unchanged.

## Test acceptance
- Python adapter tests prove that the initial connection launches **only** the identification probe, and parameter cycles launch **only** the parameter probe at the cached bitrate; timeout cleanly returns CAN DOWN.
- Node tests prove independent screen polling, profile binding, no overlap and stop on navigation, failure and expiry.
- Local native API CTests, WebGUI tests, repository doctor and CM5 release gates must pass before deployment.
- Physical acceptance: one connection, at least two consecutive FE96/FEAE refreshes with distinct capture timestamps and no F190/F188/F192 repeat, CAN DOWN after each, DTC evidence unchanged.
- Keep Issue #29 open until this final corrected flow is observed on the real CM5 kiosk.

## Operator cutover
After offline tests and committed candidate, the existing guarded installer `scripts/deploy_cm5_sac_live_gui.sh` updates only the isolated SAC adapter, static WebGUI files and its proxy. It takes backups, verifies prior hashes, rolls back on failure, and does not issue CAN messages as part of installation.

On the CM5 terminal:

```bash
cd ~/ECU_V2_INTEGRATION
sudo bash scripts/deploy_cm5_sac_live_gui.sh
```

The physical test then uses a single connect followed by multiple parameter-only cycles, rather than repeating identification.

## 2026-10-09 — Kiosk blank-parameter incident: Chromium timer receiver

**Observed after deploying commit `cbb2323c7ac1`:** identification succeeded, but all four current parameter cells remained "—". The trusted API still served an older historical FE96 measurement; that is not proof of a new measurement. CM5's static-server access log showed several successful `POST /kiosk/v1/bench/daf-sac/connect`, but **zero** requests to `/kiosk/v1/bench/daf-sac/parameters/read`.

**Root cause reproduced in real Chromium, with a fully mocked loopback API and zero CAN access:** `SacParameterMonitor` stored `setTimeout` and `clearTimeout` as bare function values and invoked them through private object properties. Chromium throws `TypeError: Illegal invocation` at `#queue` because the required Window receiver is lost. Native Node timer unit tests did not detect this browser-specific binding error.

**Minimal correction:** in the monitor constructor use arrow wrappers `(cb,ms) => globalThis.setTimeout(cb,ms)` and `(id) => globalThis.clearTimeout(id)`. All other session, API, native, DTC and CORE code remains unchanged. Browser visibility gating is preserved: do not poll when the Parameters page is hidden.

**Before fix (real Chromium, synthetic endpoint interception):** connect=1, parameter-post=0, voltage="—", JavaScript exception=Illegal invocation.

**After fix (same real Chromium harness):** connect=1, parameter-post=4, parameter-snapshot-get=4, voltage=22.4 V, JavaScript exceptions=0, `REAL_CHROMIUM_GUI_MOCKED_E2E=PASS`. The test is reproducible with `node tests/issue29_browser_smoke.cjs` and never sends to the physical CAN or production kiosk.

Deployment `scripts/deploy_cm5_sac_monitor_timer_hotfix.sh` is a **GUI-only** guarded cutover from `releases/cbb2323c7ac1`, with rollback, strict prior-revision checks, real Chromium synthetic smoke, service health and checksum verification. No ECU operation during installation. The deployed terminal must be tested separately with a physical SAC before marking Issue #29 closed.

## 2026-10-09 — faster refresh and FEAE pressure evidence
User acceptance after timer fix: FE96 voltage is visible, but refresh is too slow and both pressures have no numeric value.

The physical kiosk access log contains a successful parameter POST and API GET pair about every **2 s** with the old scheduler. The monitor previously waited **1200 ms** after each complete native operation, so reducing this idle gap to **200 ms** improves update cadence while keeping one operation at a time and preserving native FEAE receive window, CAN cleanup, and timeouts. This is not a 5 Hz sampling guarantee: each native FE96 + FEAE operation still has its physical processing time. No CORE/Bench/DUT parser changes.

Historical physical raw CAN evidence from another SAC read (500k) shows `18FEAE30 [8] FF FF FE FE FF FF FF FF`. The two pressure bytes are `0xFE`, which under SAE J1939 one-byte signal conventions is an **error indicator**; `0xFF` is the **not-available** indicator. Both are **invalid numerical pressure measurements**. The currently deployed API also reports `pgn_feae_observed=true` and `pressure1_bar=null`, `pressure2_bar=null`, but it does **not** preserve raw pressure bytes. Therefore it is not possible to assert the precise invalid reason for the current SAC without fresh raw frame evidence. No fake 0 bar, no unsupported alternate DID guessing.

WebGUI now shows explicit `NIEDOSTĘPNE` / `UNAVAILABLE` for each null pressure if PGN FEAE was observed, or `Brak FEAE` / `No FEAE` when it was not observed. Measurement units are displayed only beside valid numeric readings. These are statuses, not diagnostic descriptions of sensor failure.

Real Chromium integration test with synthetic backend: 1 initial identity, 20 parameter cycles over the test interval, numeric FE96 voltage, both pressures visibly `UNAVAILABLE`, units hidden, zero JavaScript exceptions, no physical CAN requests. Install only the static GUI assets using the guarded operator release script once committed. Issue #29 remains open for physical confirmation of rate and pressure behavior.
