# Issue #29 — DAF SAC parameter acquisition (integration candidate)

Date: 2026-10-09. Scope: PR #28, integration branch only. **Not production; no physical SAC acceptance yet.**

## Audit: existing and unchanged
- WebGUI is a read-only observer through the local kiosk proxy; only an explicit operator POST invokes the dedicated SAC connection adapter.
- The adapter verifies CAN DOWN, tries 250 then 500 kbit/s, accepts only positive F190/F188/F192 identification, runs the native read-only parameter probe and returns CAN DOWN even on failures.
- Bench Application performs FE96; the DAF SAC DUT profile filters 29-bit Classic CAN PGN FEAE (0x18FEAE30 / 0x03FFFFFF), SA 0x30, pressure bytes 2/3 at 0.08 bar/bit. J1939 invalid sentinel bytes are flagged unavailable, never interpreted as 0 bar.
- The C++ native publisher permits parameter export only after a completed operation with clean Bench cleanup and zero active leases. API uses a trust-checked, immutable filesystem snapshot (schema, owner, mode, no-follow and 24-hour age limit). HTTP API remains GET-only.
- No changes to CORE V2, Bench Runtime, DUT Profile, J1939 parser, DTC clear or production main.

## Gap and implemented remediation
- Previously the adapter returned only 'parameters_published=true' after observing a success marker. The UI matched only profile + acquisition start lower bound. This did not identify the exact completed capture and generation.
- The native probe now prints capture time (Unix ms), selected profile and Bench completion generation, **after** secure publication. The adapter accepts a completed result only if all three fields are present, unique, numeric, bounded in time, from the expected bitrate profile, and accompanied by exact PASS markers. Rejected reports are not claimed as fresh.
- Connection result includes parameter capture timestamp, generation and a typed completion status: completed, timeout, unavailable or invalid. For non-completed results timestamp is null and generation 0. Identification can succeed even when FE96 is unavailable, but previous readout is never reclassified as successful.
- WebGUI validates the connection contract, then requires **exact** profile, capture timestamp and generation from the API's immutable readout. A record from another operation, profile, DUT or bitrate is rejected; no pressure fallback to numeric zero. API errors and connection losses clear the current cells.
- Explicit 'Refresh parameters (read-only)' reuses the established 250→500 identification/measurement sequence; no implicit CAN traffic from periodic UI polling. Each new request passes through the communication and identity screens. Return from other SAC pages only displays the timestamp-validated result, and the operator can explicitly refresh.
- Values are displayed as completed, not streaming data, for at most 30 seconds after capture. Older historical snapshots retain their date/status but **not their numerical values in current-parameter cells**. The API's independent 24h historical expiry contract is unchanged.

## Acceptance evidence (offline only)
- Python adapter tests including repeated bitrate fallbacks, timing/profile/generation mismatch, timeout path, CAN-down cleanup.
- Node kiosk/client tests including strict contract, invalid/missing data, no implicit write paths.
- Linux ARM64 C++ build with -Werror; API readout wire/IPC/projection/router/HTTP CTests.
- Repository doctor offline full scope, and full available native CTest suite.
- No physical ECU commands were run to establish these results. The archived 2026-10-09 FE96 28.0/28.0 V and FEAE-unavailable pressure observations are reference baseline **not a passing test of this new candidate**.

## Physical acceptance gate, separate operator action required
1. Prepare paired release of native parameter probe, isolated adapter and kiosk WebGUI. Verify their exact checksums / version matching; save rollback snapshot. The API read-only binary and DTC evidence must remain untouched.
2. With a connected SAC and explicit operator authorization: request initial connection (250→500 fallback as applicable), confirm VIN/SW/HW, FE96 and FEAE with absent pressure represented as UNAVAILABLE; verify 0 active leases and CAN DOWN after completion.
3. Issue an explicit Refresh on the same SAC, require a *new* matching capture timestamp (not the previous one) and positive generation/provenance, and CAN DOWN. Repeat after returning to SAC from another screen.
4. Simulate lost reply/disconnection and compare API snapshot vs UI. Verify no old voltages/pressure in current cells, no synthetic zero and proper unavailable/timeout reporting. Do not disturb an active CAN owner to force a fault.
5. Verify existing archived DTC file hash/mtime untouched; service states; check no session leases and CAN DOWN. Test rollback and retain raw evidence of each physical iteration.
6. Keep Issue #29 OPEN until live evidence is collected, reviewed and accepted. Do not merge PR #28 to main without owner's explicit approval.

**Security:** prohibited UDS 0x14, output activation, reprogramming, reset, unsolicited periodic CAN requests.
