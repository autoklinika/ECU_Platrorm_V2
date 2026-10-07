# ECU Platform V2 — SAE J1939-22 C-PG / FEFF Multi-PG checkpoint

Date: 2026-10-07

Status: **NARROW TECHNICAL GATE PASS / SAE CLAUSE AUDIT REQUIRED**

## Scope

This checkpoint adds the first CAN FD application-container layer for the TRUCK / AGRI / OHV Core V2. It intentionally implements only the publicly cross-checkable no-assurance subset needed to carry ordinary Parameter Groups inside a 29-bit FEFF Multi-PG frame.

Normative baseline: SAE J1939-22_202209 — CAN FD Data Link Layer.

## Implemented subset

- CAN FD Extended Frame Format (29-bit FEFF) Multi-PG envelope,
- Multi-PG PGN 0x2500 with one shared outer destination,
- 4-byte Contained PG header,
- TOS = 2 SAE no-assurance profile,
- TF = 0 no assurance-data trailer,
- 18-bit canonical PGN encoding/decoding,
- 8-bit C-PG payload length,
- maximum 60 application bytes per C-PG,
- multiple C-PGs in one bounded 64-byte CAN FD payload,
- automatic zero padding to the next legal CAN FD DLC,
- BRS-required profile for this implementation,
- destination-specific PDU1 C-PGs using the outer Multi-PG destination,
- fixed storage only; no allocation, OS, threads or driver dependency.

Public-vector cross-check includes a global FEFF Multi-PG from SA 0x31 with identifier 0x0C25FF31 and C-PG headers `40 F1 00 04` and `40 F2 00 03`.

## Fail-closed boundaries

The decoder rejects or explicitly leaves unsupported:

- source address NULL/global for Multi-PG emission,
- NULL destination,
- non-canonical PDU1 PGNs,
- C-PG payloads above 60 bytes,
- payload overflow above 64 CAN FD bytes,
- unsupported TOS values,
- non-zero trailer format / assurance data,
- non-BRS frames for this supported profile,
- 11-bit FBFF Global Multi-PG,
- FD Transport Protocol.

Unsupported assurance profiles are not silently interpreted as ordinary C-PGs. Functional-safety assurance data is a separate concern and is not claimed by this layer.

## Explicitly not implemented

- FBFF / 11-bit Global Multi-PG,
- FD.TP.CM / FD.TP.DT,
- parallel FD transport sessions,
- EOMS/EOMA,
- assurance-data trailers,
- functional-safety profiles,
- cybersecurity assurance profiles,
- manufacturer-specific TOS/TF profiles,
- J1939-22 complete conformance.

## Verification

Executable gate:

- `ecu.core_v2.j1939.fd_cpg`

Coverage includes:

- published no-assurance C-PG header vector,
- high 18-bit PGN bits,
- 60-byte maximum C-PG,
- two-C-PG global Multi-PG public vector,
- exact outer 29-bit identifier,
- legal CAN FD DLC padding,
- destination-specific PDU1 C-PG,
- bounded overflow behavior,
- invalid source/destination,
- invalid PGN,
- unsupported TOS/TF,
- BRS boundary,
- explicit rejection of the separately gated FBFF path.

## Conformance status

SAE International identifies J1939-22_202209 as the current published CAN FD data-link-layer revision. The full licensed normative text is not stored in this repository. Therefore this checkpoint is an engineering/technical PASS only; it is not a standards-conformance claim.

J1939_22_CPG_FEFF_NO_ASSURANCE_TECHNICAL_GATE=PASS
J1939_22_FBFF_STATUS=NOT_IMPLEMENTED
J1939_22_FD_TP_STATUS=NOT_IMPLEMENTED
J1939_22_ASSURANCE_DATA_STATUS=NOT_IMPLEMENTED
J1939_22_STANDARDS_CONFORMANCE=CLAUSE_AUDIT_REQUIRED
