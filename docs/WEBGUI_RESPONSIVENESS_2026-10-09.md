# ECU Platform V2 — WebGUI responsiveness correction, 2026-10-09

Scope: **WebGUI only**, plus an operator-gated GUI release cutover. No CORE, CAN native runtime, API, ECU service, Power Manager or WVC project change. No production main merge.

## Root causes confirmed on the CM5
- The local header clock was updated from the same `setInterval(...,10000)` callback as API status requests. This is an application scheduling mistake: it means the displayed time can advance only once every ten seconds even with a completely idle CM5.
- `renderApi()` rebuilt the entire CAN interface list on every status refresh even when the CAN page was not visible. It also refreshed route-owned DTC/parameter values unnecessarily.
- Click and `hashchange` could render the same route twice. Completed parameter values were first cleared to `—` and written back within each redraw, generating avoidable DOM mutations.
- CM5 observed CPU load and free memory did **not** indicate a system bottleneck; direct frontend changes are preferred to new services, frameworks, backend timers or WVC dependencies.

## Repair
- Clock: independent 1-second `window.setInterval(updateClock,1000)`, no API/network work in its callback. Backend health remains asynchronous at 10 seconds.
- Route rendering: skip duplicate route redraw, refresh only screen-specific DOM. List CAN rendered on CAN page only; DTC on DTC page only; SAC parameters own their own monitor.
- Text outputs update only when contents change. Valid FE96/FEAE readings remain in the DOM until updated or invalidated; errors still fail closed with `—` and hidden units, no fabricated pressure zero.
- UI remains a client: static files served by an existing read-only kiosk service. No new CAN path or API command.

## Evidence
- Native Node GUI tests **52/52 PASS** including separate clock/API timers and route-owned render checks.
- Real Chromium test `node tests/issue29_browser_smoke.cjs` with fully synthetic same-origin backend confirms that wall-clock advances 2 seconds over a ~2.4-second interval while ECU identification occurs only once and parameters are updated repeatedly. No JavaScript exceptions and no physical CAN transmissions.
- Native CTest **36/36 PASS**, repository doctor **PASS**.
- Installer `scripts/deploy_cm5_sac_monitor_timer_hotfix.sh` is updated to carry `app.mjs`, monitor and localization assets (not only monitor), stop the kiosk before cutover, wait for bounded native cleanup/CAN idle, protect DTC/API/adapter checksums, restore symlink on failure, allow verified orphan release retry and report exact smoke failure reason. Six standalone installer safety tests PASS.

## Deploy and operator acceptance
From operator's interactive shell on CM5:
```bash
cd ~/ECU_V2_INTEGRATION
sudo bash scripts/deploy_cm5_sac_monitor_timer_hotfix.sh
```
Before install, leave Parameters if possible to reduce the amount of in-flight work; the installer itself stops the kiosk and waits for CAN cleanup. Verify clock seconds tick regularly, menus respond promptly, FE96 continues refreshing without re-identification, invalid FEAE channels clearly show unavailable, and DTC remains unchanged. On failure, automatic rollback restores `releases/e804cdbd67e4`.

Keep Issue #29 open until user verifies physical kiosk. No merge to production main without explicit authorization.
