# Issue #29 — screen-scoped automatic DAF SAC parameter acquisition

Date: 2026-10-09. Production main remains unchanged.

## Corrected operator requirement
The preceding Issue #29 build made **two successful physical samples**, but it refreshed only when the operator manually pressed the refresh button. This is not sufficient. While `TESTY → TRUCK → DAF → SAC → Parametry` is displayed, real FE96 / FEAE measurements must be repeated automatically. There is no need for operator clicks between measurements.

## Implemented
- `webgui/src/sac-parameter-monitor.mjs`: a client-only, single-flight polling scheduler. On entering the SAC parameter screen it calls the **existing, privileged, read-only** `/kiosk/v1/bench/daf-sac/connect` POST through `KioskSession.identifySac()`, then reads the completed parameter DTO through authenticated same-origin API GET. The native adapter itself re-identifies the DUT before every voltage/pressure probe, including 250→500 kbit/s fallback as required.
- There is **no generic WebGUI/CAN socket access**; no CORE V2 or UDS/DUT Profile modifications, no new programmable command surface. These are short native read cycles, **not a continuous raw CAN stream**. The next read is scheduled 1200 ms **after completion** of the preceding read, so effective cadence includes bus setup, identification, FE96/FEAE capture, cleanup, HTTP and transport latency.
- A new parameter reading is accepted only if physical VIN/SW/HW + selected profile/bitrate still match, and the API result matches the exact capture timestamp and Bench generation returned by that native read. Capture time must strictly increase and be within 6 s of the GUI wall clock. This prevents previous operations from appearing as a live update.
- Leaving the parameters screen, hiding the document or closing kiosk cancels scheduled future polling. An already dispatched bounded native probe is allowed to finish its own CAN-down/lease cleanup, with a late reply ignored. In-flight operations never overlap, including stop and immediate re-enter.
- Loss of communication, invalid snapshot, unavailable reading or changed DUT immediately removes displayed parameter values. After a recoverable failure the monitor backs off 4000 ms. A changed physical DUT stops automatic polling; the operator must reconnect. FEAE pressure unavailable remains `null`, never artificial zero.
- Other SAC screens are not sources of CAN polling. Global 10-s API status refresh does not overwrite parameters while monitoring. The existing button forces an immediate queued cycle (if idle) rather than navigating back to identification.
- The status line explains that displayed values come from the most recently completed cycle, and becomes unavailable if its age exceeds 6 seconds while polling.

## Offline regression
- New six targeted Node tests cover consecutive fresh captures, strict single-flight, leaving while in-flight, communication errors/invalid capture, DUT/bitrate change and unavailable pressure. Full Node WebGUI suite 47/47 PASS.
- Existing Python backend/kiosk tests and C++ API readout test suites remain valid and unchanged. The native physical tests from preceding candidate have already passed; **this GUI-only adjustment requires a separate kiosk deployment and visual operator acceptance**.

## Deployment
The operator-scoped installer `scripts/deploy_cm5_sac_live_gui.sh` only installs the new static WebGUI release and updates the static server's bounded allowlist for the monitor module. Existing native probe, privileged adapter, API V1, tokens, DTC, and Bench Agent are untouched. It verifies exact prior deployed hashes, performs protected backups, restarts only static/kiosk services, checks health and CAN DOWN, and automatically rolls back to `releases/6784769f10ff` on failure.

Run in the interactive terminal of the CM5 as `ecu`:

```bash
cd ~/ECU_V2_INTEGRATION
sudo bash scripts/deploy_cm5_sac_live_gui.sh
```

Then physically enter SAC → Parametry, observe several changing **timestamps** in sequence without clicking, and navigate away; verify CAN DOWN and no further acquisitions. On disconnection, values must disappear; pressure from unsupported channels must remain unavailable. Document the physical evidence, then close Issue #29. No merge to production main without owner approval.
