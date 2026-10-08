# ECU Platform V2 — Stage L SAC controller

## Status

**VERIFIED / PASS**

Branch:

```text
stage-l/sac-controller
```

Base:

- Stage K SAC read-only services: VERIFIED / PASS.

## Goal

Provide one SAC application-level coordinator that serializes all active UDS
operations while allowing passive CAN telemetry to continue.

This is the layer intended to sit below a future API/WebGUI boundary.

## Operations

The controller exposes:

- identification,
- DTC read,
- voltage read,
- passive pressure ingestion.

Only one active UDS operation may exist at a time.

Examples:

```text
identification active -> DTC start = BUSY
voltage active        -> identification start = BUSY
DTC active            -> voltage start = BUSY
```

Passive pressure frames can still be ingested while an active UDS operation is
running.

## State exposed upward

The controller exposes stable domain state for:

- VIN / software / hardware identification,
- DTC list,
- pressure values,
- permanent/ignition voltage,
- current active operation,
- controller status,
- last NRC / error category.

No WebGUI or HTTP framework is selected by this stage.

## Safety boundary

Stage L adds no new UDS service.

It only coordinates already verified read-oriented SAC operations.

No physical SAC communication is part of Stage L.

## Validation

Run:

```bash
./scripts/validate_stage_l_sac_controller.sh
```

Expected:

```text
STAGE_L_SAC_CONTROLLER=PASS
```

Tests verify:

- UDS-operation arbitration,
- passive pressure ingestion during active UDS,
- identification completion,
- voltage read completion,
- DTC read completion,
- state exposure,
- reset behavior.

## Merge boundary

No merge to production `main` is authorized by Stage L.


## Validation evidence — 2026-10-06

Result: **VERIFIED / PASS**

Validated on Prototype A:

- Core portability gate: PASS
- SAC module portability gate: PASS
- Debug build: PASS
- Debug CTest: `8/8` PASS
- Release build: PASS
- Release CTest: `8/8` PASS
- direct controller test: `SAC_CONTROLLER_TESTS=PASS`
- final marker: `STAGE_L_SAC_CONTROLLER=PASS`
- AddressSanitizer: PASS
- UndefinedBehaviorSanitizer: PASS
- no physical SAC transmission performed

Validated controller behavior:

- identification blocks overlapping DTC/voltage operations
- voltage read blocks overlapping identification/DTC operations
- DTC read blocks overlapping identification/voltage operations
- passive pressure frames are accepted while a UDS operation is active
- completed identification/DTC/voltage state is exposed upward
- reset clears accumulated product state

Stage L is complete.
