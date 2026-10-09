# ECU Platform V2 — WebGUI audit corrective batch

Date: 2026-10-09. Candidate branch: `integration/ecu-v2-operational-candidate-20261009`. Production `main` not merged.

## Confirmed issues and corrective changes

- **Repeated connection while an earlier parameter POST still owns CAN:** `SacParameterMonitor.whenIdle()` resolves after the native operation's browser request completes, even after `stop()`. A new `connect` waits at the existing connecting spinner. The client handles `bench_busy` with a short bounded retry instead of falsely claiming ECU communication loss. No change to the privileged server lock or CAN interface.
- **Stale voltage for >6 s while a POST hangs:** wall-clock now checks the age of the last completed record every second, independently of network callbacks. Values and units disappear after six seconds without a new verified completed capture. Client timeouts are separately bounded: 22 seconds for the parameter POST, 40 seconds for one-time 250/500 connect (GET still 4 seconds).
- **DTC modal unexpectedly closes every 10 s:** `renderDtc` compares `captured_at_unix_ms + completed_generation`. Rebuild only when a completed DTC capture changes; close details on leaving the DTC page, not on global health refresh.
- **Old DTC from another physical SAC:** a DTC record is never labeled as part of the present session unless its profile agrees with the accepted identity **and** its capture time is no earlier than that accepted connection. Because **there is no current DTC-acquisition operation in GUI**, the normal DTC screen must show “Brak odczytu DTC dla bieżącego połączenia” instead of replaying archived records. This timestamp floor is a minimal guard, not strong cryptographic DUT/session provenance; definitive binding will require native session-tagged DTC readout when DTC acquisition is implemented.
- **Same-route sidebar tap:** always closes overlay/veil even when the page itself need not rerender.
- **Terminal status lost between screens:** session-expired/profile-mismatch states are preserved and block automatic restart until explicit reconnection.
- **Repeated reading flicker:** no spurious `reading` emission on each successful cycle. `bench_busy` is a transient retry without wiping a fresh valid value; age gating remains authoritative.
- **Header bitrate jumps UP/DOWN:** during accepted SAC session use the selected identity bitrate rather than instantaneous `can0` state.
- **Empty navigation drawer:** brand/drawer button disabled on routes without SAC functions.
- **Cancel connecting:** UI cancel discards late identification; native operation already started is allowed to finish cleanup. Further queued operations check whether the request is still current before starting hardware work.
- **Duplicate navigation path:** route buttons call `navigate()` only; hashchange handles transitions. SAC phase callback no longer forcibly rerenders current page.
- **Clock drift:** schedule to the next exact wall-second boundary instead of running free `setInterval(1000)`. Backend status refresh remains independent.
- **Dynamic translation overwritten:** remove `data-i18n` from status elements controlled by JS; dynamic projections render with the selected language.
- **Redundant Refresh Parameters button:** removed; screen polling runs automatically and is bounded/sequential.
- **Mixed-version release:** operator-only release script installs the **complete 10-file static allowlist** from one clean commit, validates each file, builds a SHA-256 manifest, atomically changes the release symlink while kiosk is stopped and CAN is idle, with guarded rollback. API, native SAC probes and DTC evidence stay unchanged.

## Deferred deliberately

- `ApiSession`, token-authenticated direct API helpers and direct-loopback browser paths remain in the general reusable `api-client.mjs` module. They are not invoked by the kiosk because CSP permits only same-origin requests; removal requires a separate public-contract/API client refactor. Do **not** silently remove tested public exports in an emergency GUI fix.
- A long-lived native SAC session with CAN kept open and bounded in-memory data sampling is a **separate backend architectural decision**. The current read-only adapter opens/closes CAN, spawns a bounded probe and persists completed snapshots per operation. Avoid changing the CORE, Bench Runtime or hardware interface as part of GUI bug fixes. Any eventual runtime must preserve CAN ownership, bounded lease/recovery and no unintended ECU outputs.
- DTC clearing (Issue #30), actual current-session DTC acquisition and full diagnostic claims are out of scope.

## Real Chromium behavior tests (no physical CAN)

`node tests/issue29_browser_smoke.cjs` uses a separate headless Chromium process, separate localhost static port and **intercepts every simulated `/kiosk/v1/` backend request**. The actual CM5 production kiosk and ECU are untouched. It exercises:
1. Single connect and repeated FE96/FEAE-only cycles.
2. Prior-operation POST held while leaving Parametry; re-entry to SAC **does not start connect** until release of the old operation.
3. POST held >7 seconds; 6-second age gate removes the stale voltage even without new backend callbacks.
4. DTC captured before current connection must be refused, not displayed.
5. DTC detail modal stays open through global status refresh >10 seconds.
6. Selecting the current route inside an open drawer closes the drawer.
7. Time display advances on true wall-second alignment and JavaScript exceptions remain absent.

Also run Node, Python, native CTest and repository doctor before proposing operator deployment. Physical acceptance is still required. The static release installer never performs ECU reads, CAN actuation, DTC clearing, native deployment or merge to production.

## Operator deploy after CI and offline acceptance
```bash
cd /home/ecu/ECU_V2_INTEGRATION
sudo bash scripts/deploy_cm5_sac_monitor_timer_hotfix.sh
```
The installer is now a complete, atomic GUI release; success marker `ECU_WEBGUI_ATOMIC=PASS` (not the older timer-hotfix marker). Do not run an old release script from a different worktree.
