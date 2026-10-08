# ECU Platform V2 — Stage I SAC legacy evidence

Source boundary: read-only evidence from legacy repository
`autoklinika/ecu_platform`, branch `mcm-from-stable-sac`.

This document records facts only. Legacy architecture is not imported into V2.

## Addressing

Legacy `SAC_Definitions.h`:

```text
ECU_SA    = 0x30
TESTER_SA = 0xF9
```

Legacy `ECU_Addressing` maps those source addresses as:

```text
requestId  = 0x18DA0000 | (ECU_SA << 8)    | TESTER_SA
responseId = 0x18DA0000 | (TESTER_SA << 8) | ECU_SA
```

Therefore:

```text
tester -> SAC: 0x18DA30F9
SAC -> tester: 0x18DAF930
identifier format: 29-bit extended
```

## Legacy SAC bitrates

Legacy SAC definitions:

```text
primary   = 250000 bit/s
secondary = 500000 bit/s
```

Legacy `SACInitPage.qml` attempted connection in this order:

```text
250 kbit/s
500 kbit/s
```

This proves both profiles were supported by the legacy workflow, but does not
prove which one applies to the currently connected physical SAC.

Stage I therefore requires an explicit bitrate decision before active probing.

## Read-only identification DIDs

Legacy SAC identification used UDS service `0x22 ReadDataByIdentifier`:

```text
F190 = VIN
F188 = software identification
F192 = hardware identification
FE96 = voltage-related runtime data
```

The first V2 physical gate is deliberately limited to:

```text
22 F1 90
```

VIN read only.

## Excluded physical commands

The Stage I SAC gate does not issue:

- DiagnosticSessionControl,
- ECUReset,
- ClearDiagnosticInformation,
- SecurityAccess,
- WriteDataByIdentifier,
- RoutineControl,
- InputOutputControl,
- RequestDownload,
- TransferData,
- RequestTransferExit.

## Safety boundary

The physical gate script requires the bitrate as an explicit command-line
argument and accepts only the two legacy-evidenced values: `250000` or
`500000`.

It never performs automatic active probing across both bitrates.
