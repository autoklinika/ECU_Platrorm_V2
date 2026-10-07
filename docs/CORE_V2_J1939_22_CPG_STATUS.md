# ECU Platform V2 — SAE J1939-22 CAN FD checkpoint

Date: 2026-10-07

Status: **ENGINEERING FOUNDATION PASS / SAE CLAUSE AUDIT REQUIRED**

## Scope

This checkpoint provides the Core V2 CAN FD data-link foundation needed by
TRUCK / AGRI / OHV J1939 networks.

Implemented engineering subsets:

- FEFF 29-bit Multi-PG,
- FBFF 11-bit Global Multi-PG with AppPI = 000,
- no-assurance Contained PG (C-PG),
- no-assurance FD Transport Protocol (FD.TP),
- BAM and destination-specific RTS/CTS transfer modes,
- streaming receive/transmit interfaces with fixed Core storage.

Normative baseline: SAE J1939-22_202209 — CAN FD Data Link Layer.

The implementation is intentionally narrower than complete J1939-22. In
particular, assurance-data profiles are not interpreted.

## Multi-PG / C-PG

Supported:

- FEFF Multi-PG PGN 0x2500,
- FBFF Global Multi-PG using 11-bit identifier AppPI=000 + source address,
- FEFF global and destination-specific outer envelopes,
- 4-byte Contained PG header,
- TOS = 2 SAE no-assurance profile,
- TF = 0 no assurance-data trailer,
- canonical 18-bit PGN encoding/decoding,
- maximum 60 application bytes per C-PG,
- multiple C-PGs in one bounded 64-byte CAN FD payload,
- BRS-required profile,
- deterministic legal-DLC padding service,
- fixed storage only.

The emitted padding profile follows the public J1939-22 reference
implementation used for the engineering cross-check: the padding service starts
with up to three zero bytes; additional DLC fill bytes are 0xAA. The decoder
validates the same deterministic profile instead of accepting arbitrary tail
bytes.

FEFF destination-specific PDU1 C-PGs inherit the single outer Multi-PG
destination. FBFF is global/broadcast only.

## FD Transport Protocol

Wire-format foundation:

- FD.TP.CM PGN 0x4D00,
- FD.TP.DT PGN 0x4E00,
- 12-byte FD.TP.CM core,
- 4-byte FD.TP.DT header,
- 60 data bytes per full FD.TP.DT segment,
- 4-bit session number,
- 24-bit message size,
- 24-bit total/next segment number,
- legal CAN FD DLC rounding for final DT segment,
- 0xFF padding validation on final DT segment,
- no-assurance DTFI = 0 only.

Supported connection-management operations:

- RTS,
- CTS,
- End Of Message Status (EOMS),
- End Of Message Acknowledge (EOMA),
- BAM,
- Abort.

Supported transfer modes:

- global BAM,
- destination-specific RTS/CTS,
- CTS hold,
- block grants,
- EOMS/EOMA completion,
- remote Abort,
- timeout Abort,
- fail-closed CTS-during-data-transfer handling.

## Size and resource boundaries

Peer-to-peer FD.TP:

- minimum segmented message: 61 bytes,
- maximum encoded message size: 0xFFFFFF bytes,
- maximum segment count for 0xFFFFFF bytes: 279621,
- payload is streamed through `IFdTpTransmitSource` /
  `IFdTpReceiveSink`; Core does not allocate a 16 MiB message buffer.

BAM:

- maximum supported message: 15300 bytes,
- maximum 255 segments.

Implementation-owned concurrent session pools are deliberately bounded:

- 4 BAM RX sessions,
- 8 peer RX sessions,
- 4 BAM TX sessions,
- 8 peer TX sessions.

These are explicit engineering resource limits. They are not presented as a
complete normative concurrency claim for every possible network topology.

## Timing

The state machines use the Core V2 monotonic-clock contract and fail closed on
wrong domain, excessive timestamp uncertainty or backward time.

Current engineering timers include:

- 750 ms segment-progress timeout,
- 1250 ms peer response/data timeout,
- 1050 ms CTS hold timeout,
- 3000 ms EOMA wait timeout,
- 10 ms BAM inter-segment minimum used by the transmitter.

Exact clause-by-clause timing conformance remains part of the SAE audit gate.

## Fail-closed boundaries

Rejected or left unsupported:

- non-canonical transported PGNs,
- NULL/global source addresses,
- global destination for RTS,
- invalid session/segment numbers,
- malformed CM/DT DLC,
- inconsistent message-size / segment-count descriptors,
- wrong sequence,
- non-0 DTFI,
- non-0 assurance-data type for RTS/BAM/EOMS,
- malformed final-segment padding,
- queue overflow,
- sink/source failures,
- clock-domain/discontinuity faults,
- unsupported assurance profiles.

Unsupported assurance data is never silently reinterpreted as ordinary
application payload.

## Explicitly not implemented

- assurance-data trailers,
- functional-safety assurance profiles,
- cybersecurity assurance profiles,
- manufacturer-specific assurance profiles,
- other TOS/TF/DTFI profiles,
- nonzero FBFF AppPI profiles,
- complete clause-level J1939-22 conformance.

Functional-safety assurance is kept as a separate profile concern; it is not
mixed into the base no-assurance transport foundation.

## Verification

Executable gates:

- `ecu.core_v2.j1939.fd_cpg`,
- `ecu.core_v2.j1939.fd_tp`.

C-PG coverage includes:

- public no-assurance C-PG header vector,
- FEFF public-style Multi-PG vector,
- FBFF AppPI=000 global envelope,
- FEFF destination-specific C-PG,
- high 18-bit PGN bits,
- 60-byte C-PG,
- multiple C-PGs,
- legal DLC/padding boundaries,
- invalid source/destination/AppPI,
- unsupported TOS/TF,
- BRS boundary.

FD.TP coverage includes:

- exact 12-byte RTS wire vector,
- BAM/CTS/EOMS/EOMA/Abort codecs,
- 60-byte full DT and short final DT,
- 24-bit maximum peer-transfer metadata,
- 15300-byte BAM boundary,
- streaming BAM receive,
- streaming RTS/CTS receive,
- streaming BAM transmit,
- RTS/CTS end-to-end transfer,
- four concurrent BAM sessions,
- eight concurrent peer sessions,
- timeout boundary equality,
- backward-time failure,
- CTS during active data transfer,
- assurance-type and DTFI fail-closed paths.

## Conformance status

The public implementation and public documentation used here are engineering
cross-checks only. The licensed SAE normative text is not stored in this
repository.

Therefore:

- the no-assurance FBFF/FEFF Multi-PG engineering foundation is PASS,
- the no-assurance FD.TP engineering foundation is PASS,
- assurance profiles remain NOT IMPLEMENTED,
- full SAE J1939-22 conformance remains OPEN pending clause mapping and
  independent interoperability evidence.

J1939_22_CPG_FEFF_NO_ASSURANCE_TECHNICAL_GATE=PASS
J1939_22_CPG_FBFF_NO_ASSURANCE_TECHNICAL_GATE=PASS
J1939_22_FD_TP_NO_ASSURANCE_TECHNICAL_GATE=PASS
J1939_22_ASSURANCE_DATA_STATUS=NOT_IMPLEMENTED
J1939_22_STANDARDS_CONFORMANCE=CLAUSE_AUDIT_REQUIRED
