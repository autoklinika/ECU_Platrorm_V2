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

For future measurements, ONLY when the operator explicitly chooses:

    cd ~/ECU_API_PARAMS_V1
    sudo bash scripts/run_stage42_daf_sac_500k_read_gate.sh all --publish-readout

This uses the existing read-only Stage 4.3 physical runner and publishes
both completed parameters and DTC; no DTC erase, actuation or flashing.
Do not run physical probing automatically after deploying WebGUI/API,
do not present archival measurements as live, and do not merge to main
without explicit owner approval.
