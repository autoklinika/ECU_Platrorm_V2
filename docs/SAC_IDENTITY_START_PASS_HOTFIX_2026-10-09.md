# DAF SAC physical connection — START/PASS parser hotfix

Date: 2026-10-09
Branch: `webgui/sac-local-prototype-no-token-20261009`
Scope: ONLY the already installed root-owned 500k SAC read-only HTTP
adapter; no WebGUI, Application API V1, Bench Runtime or CORE modification.

## Reproduction on connected SAC

Operator reported working CAN but connection error in kiosk.
On the CM5, WebGUI `GET /kiosk/v1/about` was HTTP 200 while
`POST /kiosk/v1/bench/daf-sac/connect` returned HTTP 503.

One intentionally triggered fixed read-only identity operation (CM5,
2026-10-09 approx. 11:13) returned
`{"error":{"code":"invalid_identity"}}` after 0.417 s and left
`can0 DOWN`. Existing CLI physical proof already showed positive
`F190/F188/F192` from this SAC (FF17 means unprogrammed VIN,
SW=2027746, HW=K127968).

Root cause: the native C++ probe intentionally produces
`SAC_PHYSICAL_PROBE=START tx=0x18da30f9 rx=0x18daf930 bitrate=500000 mode=read-only-identification`
and finally `SAC_PHYSICAL_PROBE=PASS CORE_V2_UDS_ISOTP_CAN`.
The HTTP adapter parsed both as duplicate values of the same field and
returned `invalid_identity` even when UDS identification succeeded.

## Correction

Allow **exactly one** recognized START stage followed by exactly one
PASS result in the fixed 500 kbit/s identity parser. Validate the
fixed source/target addressing and bitrate; continue to reject
duplicates, wrong profile, missing identity markers, malformed VIN,
wrong/missing software/hardware and failed positive UDS proof.

A new regression test reproduces the exact real stdout shape and
verifies both the positive FF17 case and negative duplicate/malformed
cases. No changes to adapter route allowlist, Bearer verification,
systemd permissions, CAN setup, write prohibition or final CAN cleanup.

## Minimal CM5 installation (operator terminal)

The repository worktree must be clean on
`webgui/sac-local-prototype-no-token-20261009`.
The installer checks the exact SHA-256 of the currently installed
root-owned adapter, services healthy, `can0 DOWN`, existing protected
HTTP endpoint returning 401 without credentials, and current DTC
readout hash.

After PR/CI checks, run:

```bash
cd ~/ECU_WEBGUI_PARAMS_V1
sudo bash scripts/upgrade_cm5_sac_identity_parser.sh
```

This only atomically replaces
`/usr/local/libexec/ecu-platform-v2/sac_identify_server.py` and
restarts `ecu-sac-connect-v1.service`, with automatic restore if the
verification fails.

Manual rollback:

```bash
sudo /usr/local/sbin/ecu-sac-identity-parser-rollback
```

**Installer does NOT communicate with ECU.**
After `SAC_IDENTITY_HOTFIX=PASS`, use the kiosk:
`TESTY → TRUCK → DAF → SAC`, expect Connecting, VIN status and SW/HW,
then OK, completed parameter page. This is the next required physical
acceptance test.

No merge to production `main` without owner permission.

## Deployment attempt 2026-10-09 ~11:32 — rollback PASS

The operator reported `SAC_IDENTITY_HOTFIX=FAIL exit=2` after the
9 adapter tests passed, followed by `SAC_IDENTITY_ROLLBACK=PASS`.
Independent CM5 verification showed the original root-owned adapter
SHA unchanged (`10923ebd...`), `ecu-sac-connect-v1`, `ecu-api-v1`,
WebGUI and kiosk active, protected POST returning HTTP 401, kiosk
status returning HTTP 200, and `can0 DOWN`.

Systemd journal reports two adapter starts within the same second
(first installation restart, then rollback restart); no Python startup
exception was recorded. The old installer checked one immediate HTTP
request after `systemctl restart`. This created a plausible readiness
race, but the old script did not log which final check failed, so the
exact condition cannot be proven retrospectively.

The revised installer now polls the **unauthorized** fixed HTTP route
for a 401 response (bounded 30 × 250 ms) without performing any CAN or
UDS activity. It records `SAC_IDENTITY_HTTP_READY=PASS attempt=N`
or an explicit error and emits `stage=<name>` on failure. SHA, DTC,
CAN DOWN and rollback checks remain unchanged. Offline regression
exercises both immediate readiness and timeout. No service is restarted
automatically during this code change.
