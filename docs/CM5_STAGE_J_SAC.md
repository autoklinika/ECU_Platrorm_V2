# ECU Platform V2 — Stage J SAC module

## Status

**VERIFIED / PASS**

Branch:

```text
stage-j/sac-module
```

Base:

- Stage I UDS Core: VERIFIED / PASS,
- physical SAC UDS gate intentionally still blocked by unknown bitrate.

## Goal

Move SAC-specific knowledge out of generic Core into a dedicated ECU module.

Architecture:

```text
SAC module
  -> generic UDS client
  -> generic ISO-TP
  -> generic CAN contract
  -> platform adapter
```

The SAC module may know:

- DAF SAC CAN addressing,
- SAC DIDs,
- SAC J1939 PGNs,
- SAC-specific scaling,
- SAC identification sequence.

Generic UDS/ISO-TP Core must not know any of those details.

## Implemented profile

Legacy same-project evidence:

```text
tester -> SAC: 0x18DA30F9
SAC -> tester: 0x18DAF930
29-bit extended CAN

F190 = VIN
F188 = software identification
F192 = hardware identification
FE96 = voltage runtime data

PGN 65198 = pressure broadcast
pressure scale = raw * 0.08 bar

legacy bitrate candidates:
250 kbit/s
500 kbit/s
```

Bitrate candidates are retained as evidence only. Stage J does not implement
autobaud or active bitrate fallback.

## SAC identification

`SacIdentification` performs a read-only sequence:

```text
22 F1 90 -> VIN
22 F1 88 -> software identification
22 F1 92 -> hardware identification
```

It uses the generic `UdsClient` and does not know about SocketCAN, Linux,
`can0` or device configuration.

Responses are validated for:

- positive SID `0x62`,
- expected DID,
- non-empty printable ASCII payload,
- bounded fixed-size destination buffers.

Negative UDS responses preserve the NRC for higher-layer diagnostics.

## SAC runtime decoders

### Pressure broadcast

The module contains a stateless J1939 PGN decoder and recognizes legacy SAC:

```text
PGN 65198
data[2] -> pressure 1, scale 0.08 bar
data[3] -> pressure 2, scale 0.08 bar
```

UDS PF `0xDA` traffic is explicitly excluded from the pressure decoder.

### Voltage DID

The module parses positive response:

```text
62 FE 96 ...
```

using the legacy byte layout for permanent and ignition voltage, scaled by 0.1 V.

## Safety boundary

Stage J does not send anything to the connected SAC.

It is a code/test stage only.

Physical transmission remains blocked until bitrate is deliberately chosen or
a later safe autobaud solution is implemented.

## Validation

Run:

```bash
./scripts/validate_stage_j_sac.sh
```

Expected marker:

```text
STAGE_J_SAC=PASS
```

Validation includes:

- Core portability guard,
- separate SAC-module portability guard,
- Debug build + tests,
- Release build + tests,
- direct SAC module test.

## Merge boundary

No merge to production `main` is authorized by Stage J.


## Validation evidence — 2026-10-06

Result: **VERIFIED / PASS**

Validated on Prototype A:

- Core portability gate: PASS
- SAC module portability gate: PASS
- Debug build: PASS
- Debug CTest: `6/6` PASS
- Release build: PASS
- Release CTest: `6/6` PASS
- direct module test: `SAC_MODULE_TESTS=PASS`
- final marker: `STAGE_J_SAC=PASS`
- AddressSanitizer: PASS
- UndefinedBehaviorSanitizer: PASS
- no new OS packages installed
- no physical SAC request transmitted

Validated SAC behavior:

- fixed 29-bit diagnostic IDs
- VIN/SW/HW read-only identification sequence through real UDS + ISO-TP Core
- NRC preservation
- J1939 PGN 65198 pressure decode
- UDS PF 0xDA exclusion from passive pressure decoder
- FE96 voltage response decode
- separate module portability boundary

Stage J is complete.
