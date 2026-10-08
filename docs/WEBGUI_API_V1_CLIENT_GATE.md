# WebGUI / API V1 — client-only integration gate (2026-10-08)

Status: candidate branch; NOT deployed, NOT merged into main. Based on
WebGUI PR #19; API implementation stays independent in PR #20.

## Ownership and architectural boundary

- A single browser module src/api-client.mjs owns API access.
  Exact origin: http://127.0.0.1:8878.
  Fixed GET allowlist: /api/v1/about, /api/v1/interfaces,
  /api/v1/dut, /api/v1/readouts/dtc/latest.
- The static server is restricted to http://127.0.0.1:8877.
  Both HTML and HTTP Content Security Policy allow connections only to self
  and the exact local API origin. No remote host, arbitrary proxy, service,
  CAN channel or agent request is possible through the UI.
- The API is an independent unprivileged service; the kiosk has no
  privileged Bench Agent socket access, CAN devices or raw CAN permissions.
- Operator enters the authorized 64-char bearer token in Settings.
  Password input is cleared immediately after submission; token is never
  bundled, persisted, put in a URL, copied to localStorage/sessionStorage
  or exposed by static HTML. Login requires an authenticated /about GET.
  Session exists in private JS memory only, expires after 15 minutes,
  and is discarded on logout, pagehide and 401/403. Network requests
  use Authorization header; credentials/cookies omitted and cache disabled.
- Only language preferences are persisted. This is a manually gated
  laboratory V1 login, not a general multi-user identity system.

## Display semantics and failure safety

- Header “API connected” only means successful /about, never ECU presence.
- CAN view reports read-only kernel interface link state, configured
  nominal/data bitrates and bus-off condition; CAN DOWN is not live traffic.
- Selected DUT profile may be shown only if an authoritative /dut provider
  exists; this does not establish physical module presence.
- DTC screen displays only a completed historical application readout:
  source=completed_application_operation, live=false, capture time,
  profile, protocol, codes, status bytes. It cannot read live DTCs.
- Missing readout 503 is NOT zero faults; expired readout 410 is NOT an
  active ECU result. Invalid schemas, stale responses, mismatched auth,
  API downtime and network errors show unavailable, never fake values.
- Values from HTTP are validated before rendering and inserted with
  textContent/createElement, never unescaped HTML.
- Polling every 10 seconds is GET-only, authenticated and bounded
  by request timeout; no polling before authentication.

## Nonprivileged validation

Commands from the separate candidate worktree on CM5:

    node --check webgui/src/app.mjs
    node --check webgui/src/api-client.mjs
    node --test webgui/tests/*.test.mjs
    python3 -m unittest discover -s tests -p test_webgui_install_contract.py -v
    bash -n scripts/deploy_cm5_webgui_api_v1.sh

All unit/contract fixtures are synthetic; no physical SAC read, CAN
transmission, or actuator/power action is part of validation.

## Controlled operator-only release

Only after source review, GitHub CI and verification of an existing
API V1 installation on CM5, operator runs in an interactive VS Code
terminal:

    cd ~/ECU_WebGUI_API_V1
    sudo bash scripts/deploy_cm5_webgui_api_v1.sh

Deployment checks root/TTY, clean expected Git branch, running kiosk,
WebGUI static server, API, Bench Agent and can0 DOWN. It stages a new
root-owned release, updates the strictly allowlisted static-server
assets and CSP, switches current symlink atomically, and restarts only
ecu-webgui-static and ecu-kiosk. It checks HTTP, CSP, API unauthorized
401 and unchanged CAN state. On post-cutover failure, it restores
previous static-server code and release pointer and restarts only
the two WebGUI services. Neither API nor Bench Agent is restarted.

Root-owned scoped rollback entrypoint after installation:

    sudo /usr/local/sbin/ecu-webgui-api-v1-rollback

After deployment the human operator must verify touch, hidden cursor,
reboot, no unauthenticated requests, valid operator login, CAN DOWN
presentation and correct handling of no archived DTC. No automatic
provisioning of the long-lived API token to the kiosk is authorized.
Do not share, log, screenshot, paste into a chat, embed in JS or commit
the token. Automatic login would require a separate short-lived
pairing/identity protocol with its own review and API changes.

## Explicit exclusions

No CORE changes, no modified Application API service, no new
physical ECU read, no bench command or relay/power control, no
DTC erase, no current-session DTC provider, no main merge.
