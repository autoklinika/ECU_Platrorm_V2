# ECU Platform V2 — local kiosk, no operator token (prototype)

Date: 2026-10-09. Branch: `webgui/sac-local-prototype-no-token-20261009`.
Depends on installed WebGUI SAC identification branch `5e354baebe1d`.
Draft candidate, **not merged into main**.

## Operator workflow

No login, no manual token and no new credentials in the touchscreen interface.
On kiosk boot, status, CAN readout and historical SAC data are retrieved
automatically from the same-origin `http://127.0.0.1:8877/kiosk/v1/*`
routes. Click `TESTY → TRUCK → DAF → SAC` to run the exact, existing
fixed-profile read-only identification operation, show VIN/SW/HW and require
**OK** to enter the parameter page. Connection failure shows the error
screen with retry/back. This is an operator-initiated hardware action;
ordinary GET/refresh does not trigger a CAN operation.

## Auth boundary — token not removed from protected services

- Browser: **no bearer token** or secret, no token form, no local credential
  persistence, no cross-origin HTTP. CSP is `connect-src 'self'`.
- WebGUI static service: still `DynamicUser=yes`, no CAP_NET_ADMIN,
  `PrivateDevices=yes`, loopback-only. It has only a fixed route allowlist.
  It gets the **existing, unchanged API token** through the systemd
  `LoadCredential` service directive. No secret is embedded in any static
  asset, script, URL or repository.
- API V1 (port 8878) remains protected by Bearer and GET-only. The
  fixed-profile SAC read-only adapter (port 8879) **also keeps its Bearer**
  verification and privileged CAN controls unchanged.
- The kiosk service adds the internal credential to the outbound request,
  forwarding only GET about/interfaces/dut/completed readouts and the single
  fixed SAC read-only POST. It validates Host, kiosk-specific header, empty
  request body, and (for POST) exact kiosk Origin. Missing credentials or
  malformed upstream response fail closed.
- Explicit **prototype limitation:** any authorized local process able to
  make same-host requests with the kiosk headers can access this local
  functionality. This is an OS-local single-user kiosk trust model, not
  suitable for a network-exposed or multi-user deployment. No web
  application should be permitted to expose the loopback port remotely.

## Operator installation / rollback

The older kiosk release `5e354baebe1d` and the SAC adapter must already
be installed. The deployer checks that CAN is DOWN, all existing services
are healthy, the selected worktree is clean, and the V1 direct endpoints
are still protected. It installs only a new immutable static release,
updates the static Python host, and adds one systemd drop-in to feed the
existing token to the unprivileged static service.

**After CI and code review**, from the CM5 VS Code terminal:

```bash
cd ~/ECU_WEBGUI_PARAMS_V1
sudo bash scripts/deploy_cm5_sac_kiosk_no_token.sh
```

The installer restarts only `ecu-webgui-static` and `ecu-kiosk`, not
the ECU API, SAC adapter, Bench Agent, or CAN. It checks that kiosk
`/kiosk/v1/about` returns HTTP 200, while direct API V1 and SAC adapter
without a bearer still return 401, and invalid-origin POST is rejected
**without any physical CAN probe**. It verifies the previous DTC readout
has not changed and that `can0` remains DOWN.

If any post-installation smoke fails, it automatically restores the
old static assets and systemd configuration. Manual rollback:

```bash
sudo /usr/local/sbin/ecu-sac-kiosk-rollback
```

## Testing

Static and offline checks include Node WebGUI navigation/session/UI tests;
Python same-origin proxy tests with stub upstream (no real CAN); earlier
SAC operator adapter security tests; shell syntax and deployment refusal;
and cross-platform CI. The GUI → static credential proxy → real fixed-profile
adapter → DUT is accepted only after the operator explicitly performs
the tap-to-connect physical test on the kiosk.

No CORE or Bench Runtime change. No physical test has been triggered by
implementation, CI, deployment or health checks.
