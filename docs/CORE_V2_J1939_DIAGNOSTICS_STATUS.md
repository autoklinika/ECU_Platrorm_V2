# ECU Platform V2 — J1939-73 diagnostics checkpoint

Date: 2026-10-07

Status: **READ-ONLY TECHNICAL GATE PASS / SAE CLAUSE AUDIT REQUIRED**

## Scope

This checkpoint adds a bounded, transport-independent read-only diagnostic foundation for TRUCK / AGRI / OHV J1939 networks.

Implemented PGNs:

- DM1 / PGN 65226 / 0xFECA — active diagnostic trouble codes,
- DM2 / PGN 65227 / 0xFECB — previously active diagnostic trouble codes.

This module does not implement destructive or control-oriented diagnostic messages.

## Implemented behavior

- single-frame 8-byte DM1/DM2 parsing,
- Classical TP-reassembled DM1/DM2 parsing,
- diagnostic lamp status decoding,
- diagnostic lamp flash-state decoding,
- 19-bit current-method SPN decoding,
- 5-bit FMI decoding,
- 7-bit occurrence-count decoding,
- conversion-method bit exposure,
- fail-closed handling of legacy/unsupported conversion method,
- bounded DTC indexing with no dynamic allocation,
- exact single-frame filler validation,
- global-source rejection and valid claimed-source requirement,
- transported DM validation as broadcast/global traffic.

The parser deliberately does not reinterpret a legacy conversion-method DTC as a current-format SPN. It returns an explicit unsupported status instead of inventing a value.

## Explicitly not implemented

- clear/reset DTC commands,
- DM3/DM11 clearing behavior,
- diagnostic broadcast-control commands,
- memory access,
- actuator control,
- security/access control,
- proprietary diagnostic messages,
- complete J1939-73 message set.

Those capabilities require separate module gates and, for state-changing operations, explicit safety/policy authorization.

## Architecture

The diagnostic layer consumes either one validated Classical J1939 CAN frame or one already reassembled TpMessage. It does not own the CAN driver, TP state machine, thread, timer, filesystem or heap storage. The DTC decoder is stateless and bounded.

## Verification

Executable gate: ecu.core_v2.j1939.diagnostics

Coverage includes zero-DTC DM1, one-DTC single-frame DM1, two-DTC transported DM1, transported DM2, lamp/flash fields, high 19-bit SPN, FMI and occurrence count, unsupported conversion-method fail-closed path, invalid DTC index, malformed DLC/filler, global source rejection, malformed TP length, non-broadcast transported DM and unrelated PGN rejection.

## Normative/reference status

Normative baseline: SAE J1939-73_202609 — Application Layer — Diagnostics.

The full licensed SAE normative text is not stored in this repository. Therefore this checkpoint is an engineering/technical PASS only. A clause-by-clause audit and independent interoperability evidence remain mandatory before standards-level PASS.

J1939_DIAGNOSTICS_DM1_DM2_TECHNICAL_GATE=PASS
J1939_DIAGNOSTICS_STATE_CHANGING_SERVICES=NOT_IMPLEMENTED
J1939_DIAGNOSTICS_STANDARDS_CONFORMANCE=CLAUSE_AUDIT_REQUIRED
