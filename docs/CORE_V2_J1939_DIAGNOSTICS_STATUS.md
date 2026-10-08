# ECU Platform V2 — J1939-73 diagnostics checkpoint

Date: 2026-10-07

Status: **READ-ONLY TECHNICAL GATE PASS / SAE CLAUSE AUDIT REQUIRED**

## Scope

This checkpoint provides a bounded, transport-independent read-only diagnostic
foundation for TRUCK / AGRI / OHV J1939 networks.

Implemented read-only PGNs:

- DM1 / PGN 65226 / 0xFECA — Active Diagnostic Trouble Codes,
- DM2 / PGN 65227 / 0xFECB — Previously Active Diagnostic Trouble Codes,
- DM4 / PGN 65229 / 0xFECD — Freeze Frame Parameters,
- DM5 / PGN 65230 / 0xFECE — Diagnostic Readiness 1,
- DM6 / PGN 65231 / 0xFECF — Emission-Related Pending Diagnostic Trouble Codes,
- DM12 / PGN 65236 / 0xFED4 — Emission-Related MIL-On Diagnostic Trouble Codes.

This module does not implement destructive or control-oriented diagnostic
messages.

## DTC-list behavior

DM1, DM2, DM6 and DM12 share one bounded DTC-list decoder:

- single-frame 8-byte parsing,
- Classical TP-reassembled parsing,
- BAM/global and destination-specific RTS/CTS response envelopes,
- diagnostic lamp status and flash-state decoding,
- 19-bit current-method SPN decoding,
- 5-bit FMI decoding,
- 7-bit occurrence-count decoding,
- conversion-method bit exposure,
- fail-closed handling of legacy/unsupported conversion method,
- bounded DTC indexing with no dynamic allocation,
- exact single-frame filler validation,
- global-source rejection and valid claimed-source requirement,
- TP message length and destination validation.

Destination-specific TP acceptance is intentional for requested diagnostic
responses. A peer TP message must use a claimable destination different from
the source; broadcast TP remains restricted to the global destination.

## DM4 Freeze Frame Parameters

The DM4 reader supports:

- exact no-DTC single-frame response validation,
- variable-length freeze-frame records after Classical TP reassembly,
- multiple records in one reassembled message,
- bounded record scanning without dynamic allocation,
- DTC decode using the shared DTC primitive,
- raw Engine Torque Mode,
- raw Boost,
- raw Engine Speed,
- raw Engine Load,
- raw Engine Coolant Temperature,
- raw Vehicle Speed,
- bounded manufacturer-specific tail location/length.

The parser exposes the J1939-71 snapshot fields as raw encoded values. It does
not apply engineering-unit scaling inside the J1939-73 layer; J1939-71 scaling
belongs to the application/SPN interpretation layer.

A malformed length field, truncated record or unsupported legacy DTC conversion
method fails closed.

## DM5 Diagnostic Readiness 1

The fixed 8-byte DM5 parser exposes without reinterpretation:

- active DTC count,
- previously active DTC count,
- OBD compliance byte,
- continuously monitored systems support/status byte,
- non-continuously monitored systems support bytes 5 and 6,
- non-continuously monitored systems status bytes 7 and 8.

The source address and exact DLC are validated.

## Explicitly not implemented

- clear/reset DTC commands,
- DM3/DM11 clearing behavior,
- DM7 or other command-oriented diagnostic test execution,
- diagnostic broadcast-control commands,
- memory access,
- actuator control,
- security/access control,
- proprietary diagnostic messages,
- complete J1939-73 message set,
- J1939-71 engineering-unit conversion for DM4 snapshots.

Those capabilities require separate module gates and, for state-changing
operations, explicit safety/policy authorization.

## Architecture

The diagnostic layer consumes either one validated Classical J1939 CAN frame or
one already reassembled `TpMessage`.

On-request reads use the shared PGN 59904 Request codec. PGN 59392 responses are
handled by the shared read-only Acknowledgment parser. The diagnostic layer does
not own a CAN driver, TP state machine, thread, timer, filesystem or heap
storage. DTC and freeze-frame decoders are stateless and bounded.

## Verification

Executable gate:

- `ecu.core_v2.j1939.diagnostics`

Coverage includes:

- zero-DTC and one-DTC single-frame DTC lists,
- multi-DTC transported messages,
- DM1, DM2, DM6 and DM12,
- BAM/global and destination-specific TP envelopes,
- lamp/flash fields,
- high 19-bit SPN, FMI and occurrence count,
- unsupported conversion-method fail-closed paths,
- invalid DTC index,
- malformed DLC/filler/source/destination,
- DM4 no-DTC response,
- multi-record DM4 with manufacturer-specific tail,
- malformed/truncated DM4 records,
- DM4 destination-specific TP,
- DM5 field decoding and negative cases.

The full Core V2 validation matrix remains green in Debug, Release, Generic
non-Linux and sanitizer builds.

## Normative/reference status

Normative baseline: SAE J1939-73_202609 — Application Layer — Diagnostics.

Public cross-checks were used only to corroborate PGN/function mappings and the
older publicly visible DM4/DM5 field layout. They do not replace the licensed
SAE normative text.

The full licensed SAE normative text is not stored in this repository.
Therefore this checkpoint is an engineering/technical PASS only. A
clause-by-clause audit against the current SAE J1939-73 edition and independent
interoperability evidence remain mandatory before standards-level PASS.

J1939_DIAGNOSTICS_READ_ONLY_TECHNICAL_GATE=PASS
J1939_DIAGNOSTICS_IMPLEMENTED=DM1,DM2,DM4,DM5,DM6,DM12
J1939_DIAGNOSTICS_STATE_CHANGING_SERVICES=NOT_IMPLEMENTED
J1939_DIAGNOSTICS_STANDARDS_CONFORMANCE=CLAUSE_AUDIT_REQUIRED
