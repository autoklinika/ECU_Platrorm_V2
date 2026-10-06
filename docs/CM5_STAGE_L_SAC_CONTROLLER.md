# ECU Platform V2 — Stage L SAC controller

## Status

**IMPLEMENTATION / VALIDATION IN PROGRESS**

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
