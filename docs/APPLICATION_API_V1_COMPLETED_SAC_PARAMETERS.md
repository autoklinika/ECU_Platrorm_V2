# Application API V1 — completed DAF SAC parameters

Status: isolated candidate on branch api/v1-sac-parameters-readout-20261008.
Not installed on the working CM5, not merged into main.
Based on API foundation PR #20.

## One established diagnostic path; no duplicated hardware logic

The DAF SAC Application Layer already performs a completed read-only
measurement using UDS 22 FE96 for both supply voltages and passively
received PGN FEAE for two pressure channels. Its Bench Runtime owns
CAN resources and releases leases after completion.

The existing Stage 4.3 operator runner is extended to publish a
separate completed parameter result on the SAME successful physical
parameters operation already executed during optional mode
all --publish-readout. Existing DTC publishing is unchanged.
No new CAN driver, session manager, ECU query, privileged proxy or
background telemetry service is added.

## Data and security contract

Portable C++17 struct CompletedSacParameters contains:
- capture time (Unix milliseconds);
- validated DAF SAC 250k or 500k profile ID;
- nonzero completed Bench operation generation;
- permanent and ignition voltage in decivolts (0–600);
- PGN FEAE reception flag;
- optional pressure 1/2 in centibar (0–2024).
Pressure FE/FF remains unavailable, not 0 bar.

Projection rejects incomplete native state, false validity flags,
unfinished leases, wrong bitrate/profile, NaN/out-of-range data and
resource-cleanup errors. Results are not live telemetry.

The producer writes named file sac-parameters-latest.v1 in the
already installed private readout directory. Wire version is
ECU_COMPLETED_SAC_PARAMETERS_V1. It reuses EXACTLY the existing
DTC store's bounded parsing, nofollow path traversal, strict UID/GID
and mode checks, regular-file/hard-link checks, atomic rename, fsync
and 24-hour freshness rules. Previous dtc-latest.v1 is unchanged.

Read-only API endpoint (Bearer, loopback and strict Origin):
GET /api/v1/readouts/daf-sac/parameters/latest

An example synthetically generated, not live, JSON response:

    {"schema_version":1,"data":{
      "source":"completed_application_operation","live":false,
      "captured_at_unix_ms":1791492382691,
      "profile_id":3673161808,"completed_generation":1,
      "parameters":{"permanent_voltage_v":27.9,
        "ignition_voltage_v":27.9,"pgn_feae_observed":true,
        "pressure1_bar":null,"pressure2_bar":null}
    }}

Missing is 503, expired 410, malformed/unsafe 502, unauthorized 401,
wrong Host/Origin 403; control methods stay forbidden.
The physical pressure calibration for FEAE is not yet independently
confirmed against a known pressure.

## Existing real physical evidence — do not repeat it needlessly

The operator already verified the 500k SAC on 2026-10-08:
27.9 V permanent, 27.9 V ignition, PGN observed but both pressures
unavailable; DTC 13, no destructive operations, zero leases and can0
DOWN. The completed DTC result is published in the installed API
and must remain intact. The old probe did not publish parameters.

Do not silently recreate a fresh native Application Layer snapshot
from archived CLI output. The new endpoint will correctly return
503 until a new authorized completed parameter result is published;
CI never performs a physical CAN read.

## Build and staged upgrade

After all PR and CI gates are green, the operator on CM5 runs:

    cd ~/ECU_API_PARAMS_V1
    bash scripts/prepare_cm5_api_parameters_v1.sh
    sudo bash scripts/upgrade_cm5_api_sac_parameters_v1.sh

The upgrade requires an interactive root terminal, exact clean branch
commit, candidate binary SHA, protected existing token, healthy API,
kiosk and Bench Agent, CAN DOWN and preserved existing DTC snapshot.
It atomically replaces ONLY the installed API executable. No service
units, accounts, credentials, privileged Bench Agent or kernel CAN
settings are modified. Smoke checks live API HTTP auth/CORS and
the exact build revision. Previous executable is backed up and
automatically restored if the post-upgrade gate fails.

Recovery, after this upgrade:

    sudo /usr/local/sbin/ecu-api-parameters-rollback

For just the first actual measurement snapshot after this software update,
the operator can explicitly choose the existing physical runner with the
new targeted mode (requires the current prepared and hashed build):

    cd ~/ECU_API_PARAMS_V1
    bash scripts/prepare_cm5_api_parameters_v1.sh
    sudo bash scripts/run_stage42_daf_sac_500k_read_gate.sh parameters --publish-readout

It uses the same read-only Stage 4.3 hardware/Application Layer composition:
passive listen-only phase, DUT identification, one completed read of
parameters and mandatory CAN cleanup. The existing published DTC list is
neither queried nor overwritten. After publication, the GUI shows the
historical capture time, the actual two supply voltages, and null pressure
where FE/FF is decoded as unavailable.

To intentionally refresh BOTH parameter and DTC snapshots later, the
separate explicit choice remains:

    sudo bash scripts/run_stage42_daf_sac_500k_read_gate.sh all --publish-readout

Neither mode performs erase, actuation or programming. Do not run a probe
just for deployment or CI; this is a real ECU operation.
Do not run physical probing automatically after deploying WebGUI/API,
do not present archival measurements as live, and do not merge to main
without explicit owner approval.

## 2026-10-09 — isolated 500k physical parameter snapshot blocked

Operator run `parameters --publish-readout` aborted during its 5-second
`LISTEN-ONLY` phase: RX packets +16255, kernel RX errors +1, TX
packets +0, candump data frames 16265, CAN error frames 1. Private
passive evidence was retained as
`read-500k-20261009T065052Z-24270.passive.log`.

The actual SocketCAN error is `CAN_ERR_CRTL` (`0x20000004`),
`controller-problem{rx-overflow}`, at the MCP251xFD receive path;
**not a decoded bus CRC/bit-stuff/ACK error**. Of 16265 candump data
frames, 16264 carried repeated J1939 TP.DT `0x18EBFF30`.
This is consistent with a single ECU repeatedly transmitting without
an ACK partner while the bench receiver is deliberately listen-only.
That mechanism remains a hypothesis; source of repeated TP.DT not
independently proven.

No UDS identification, parameter probe or publication was attempted
by this failed run. The historical DTC snapshot stays unchanged, and
`can0` was verified DOWN afterwards. A previous physical `all` test
at 2026-10-08 22:46 succeeded with zero RX errors.

The initial incident patch classified a one-off overflow as hard FAIL,
stopping *before* the UDS identification; this was excessively strict
for the already agreed single-ECU laboratory arrangement.

**Revised acceptance contract (operator decision, 2026-10-09):**
Passive capture is observational, not communication proof, and ACK is
not an acceptance criterion with one DUT on the bench. A precisely
classified receive overflow (`RX_OVERFLOW_EVENTS == ERROR_FRAMES ==
RX_ERRORS_DELTA > 0`, TX delta 0) becomes a visible **WARNING** and
allows the already authorized read-only identification to proceed.
Unexpected TX, other/unclassified receive errors and BUS-OFF still block.
Only a fully positive ISO-TP/UDS identification of `F190/F188/F192`
establishes `SAC_500K_COMMUNICATION_PROOF=PASS` before any parameter
or DTC read. The previously observed `F190 = FF*17` is a valid
*unprogrammed VIN marker*, not a valid VIN value; F188 and F192 must
still decode correctly. No passive result alone means communication PASS.

No read/probe/retry is run automatically. This change affects only the
isolated operator-controlled 500k SAC script, not Core V2, Bench Runtime,
installed APIs or service configuration.
