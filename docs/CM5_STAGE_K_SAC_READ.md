# ECU Platform V2 — Stage K SAC read-only services

## Status

**VERIFIED / PASS**

Branch:

```text
stage-k/sac-read-services
```

Base:

- Stage J SAC module: VERIFIED / PASS.

## Goal

Extend the SAC-specific product module with read-only service workflows while
keeping all destructive/actuator operations excluded.

## DTC reader

`SacDtcReader` implements the legacy-confirmed read sequence:

```text
10 03          -> extended diagnostic session
19 02 <mask>   -> ReadDTCInformation / report DTC by status mask
```

Default status mask:

```text
FF
```

The reader:

- validates positive session response `50 03`,
- uses generic UDS response/NRC handling,
- validates `59 02`,
- parses 3-byte DTC + 1-byte status records,
- stores up to 128 records in fixed storage,
- preserves NRC on negative response.

It does **not** implement ClearDiagnosticInformation.

## Runtime monitor

`SacRuntimeMonitor` combines two read-only data paths:

1. passive J1939 pressure broadcast ingestion,
2. explicitly requested UDS voltage read via DID `FE96`.

Voltage reads are not scheduled automatically. The caller must invoke:

```text
start_voltage_read()
```

This avoids hidden periodic traffic while the V2 runtime/lifecycle layer is still
being designed.

## Generic UDS addition

Stage K adds one generic request builder:

```text
19 02 <statusMask>
```

for ReadDTCInformation by status mask.

No write/reset/security/routine/output/flash service is added.

## Safety boundary

No physical SAC transmission is part of Stage K.

The connected SAC remains untouched until bitrate is deliberately selected or a
future safe autobaud mechanism is available.

## Validation

Run:

```bash
./scripts/validate_stage_k_sac_read.sh
```

Expected marker:

```text
STAGE_K_SAC_READ=PASS
```

Tests cover:

- generic 0x19/0x02 request builder,
- extended-session handshake,
- positive DTC parsing,
- NRC preservation,
- passive pressure ingest,
- explicit FE96 voltage request,
- prevention of overlapping voltage requests.

## Merge boundary

No merge to production `main` is authorized by Stage K.


## Validation evidence — 2026-10-06

Result: **VERIFIED / PASS**

Validated on Prototype A:

- Core portability gate: PASS
- SAC module portability gate: PASS
- Debug build: PASS
- Debug CTest: `7/7` PASS
- Release build: PASS
- Release CTest: `7/7` PASS
- direct read-services test: `SAC_READ_SERVICES_TESTS=PASS`
- final marker: `STAGE_K_SAC_READ=PASS`
- AddressSanitizer: PASS
- UndefinedBehaviorSanitizer: PASS
- no physical SAC transmission performed

Validated workflows:

- generic UDS `19 02 <mask>` builder
- SAC extended-session entry `10 03`
- positive DTC read and fixed-record parsing
- NRC preservation
- explicit FE96 voltage request
- prevention of overlapping voltage requests
- passive PGN pressure ingest remains independent of UDS traffic

Stage K is complete.
