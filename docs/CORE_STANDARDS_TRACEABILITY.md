[Reading 169 lines from start (total: 169 lines, 0 remaining)]

# ECU Platform V2 — Standards Traceability Baseline

Date: 2026-10-06

Status: ACTIVE / MANDATORY FOR CORE HARDENING

This document defines the standards baseline for ECU Platform V2 Core. A passing
unit/integration test is not equivalent to standards conformance.

## Conformance policy

For every protocol or safety/security-relevant Core capability we require:

1. an identified publisher and exact standard/recommended-practice edition,
2. a defined implementation scope,
3. requirement/clause-to-code traceability,
4. requirement/clause-to-test traceability,
5. negative/boundary/interoperability evidence,
6. a recorded conformance status.

Status levels:

- BASELINE_VERIFIED: current official publication/edition has been verified.
- IMPLEMENTED_PARTIAL: code exists, but complete clause-level audit is not closed.
- CLAUSE_AUDIT_REQUIRED: licensed/full normative text must be checked requirement by requirement.
- CONFORMANCE_TESTED: applicable mapped requirements have executable evidence.
- PASS: all applicable requirements in the declared scope are closed.
- N/A: standard is outside the declared product scope, with rationale.

No stage may be called standards-conformant solely because CTest, sanitizers,
portability, architecture, or hardware smoke tests pass.

## Current normative baseline

| Domain | Normative/reference baseline | Project use | Current status |
|---|---|---|---|
| CAN / CAN FD data link | ISO 11898-1:2024 | CAN/CAN-FD frame semantics and data-link assumptions | BASELINE_VERIFIED / CLAUSE_AUDIT_REQUIRED |
| High-speed CAN physical layer | ISO 11898-2:2026 | hardware/transceiver/physical integration | BASELINE_VERIFIED / CLAUSE_AUDIT_REQUIRED |
| DoCAN / ISO-TP | ISO 15765-2:2024 | transport and network layer over CAN | IMPLEMENTED_PARTIAL / CLAUSE_AUDIT_REQUIRED |
| UDS application layer | ISO 14229-1:2026 | diagnostic services and response semantics | IMPLEMENTED_PARTIAL / CLAUSE_AUDIT_REQUIRED |
| UDS session layer | ISO 14229-2:2021 | transport-independent session services/timing model | IMPLEMENTED_PARTIAL / CLAUSE_AUDIT_REQUIRED |
| UDS on CAN | ISO 14229-3:2022 | UDS implementation profile on CAN | IMPLEMENTED_PARTIAL / CLAUSE_AUDIT_REQUIRED |
| AUTOSAR diagnostic cross-check | AUTOSAR Classic Platform DCM, R24-11 or newer verified release | secondary implementation cross-check; never replaces ISO | REFERENCE_ONLY |
| DoIP transport/network | ISO 13400-2:2025 | future first-class diagnostic transport | NOT_IMPLEMENTED |
| DoIP wired interface | ISO 13400-3:2016 | test-equipment/vehicle wired Ethernet interface | NOT_IMPLEMENTED |
| UDS on IP | ISO 14229-5:2022 | UDS implementation profile over IP | NOT_IMPLEMENTED |
| J1939 Classical data link / transport | SAE J1939/21_202205 | 29-bit identifiers, PGN semantics, classical transport | IMPLEMENTED_PARTIAL / CLAUSE_AUDIT_REQUIRED |
| J1939 CAN FD | SAE J1939-22_202209 | future J1939 over CAN FD | NOT_IMPLEMENTED |
| J1939 network management | SAE J1939/81_202504 | Address Claiming and network management | NOT_IMPLEMENTED |
| J1939 diagnostics | SAE J1939-73_202609 | J1939 diagnostic messages/services | NOT_IMPLEMENTED |
| Cybersecurity engineering | ISO/SAE 21434:2021 | lifecycle security engineering where applicable | BASELINE_VERIFIED / PROCESS_NOT_CLOSED |
| Vehicle cyber regulation reference | UN Regulation No. 155 | regulatory reference where product/vehicle scope applies | SCOPE_ASSESSMENT_REQUIRED |
| Software update engineering | ISO 24089:2023 + Amd 1:2024 | update package/integrity/update lifecycle | NOT_IMPLEMENTED |
| Vehicle software-update regulation reference | UN Regulation No. 156 | regulatory reference where product/vehicle scope applies | SCOPE_ASSESSMENT_REQUIRED |
| Functional safety | ISO 26262:2018 series, especially Part 6 for software | applies only to declared safety-related road-vehicle scope; principles may be adopted more broadly | SCOPE_ASSESSMENT_REQUIRED |

Notes:
- ISO 15765-2 Edition 5 is under development in 2026; ISO 15765-2:2024 remains
  the published normative baseline until a replacement is published.
- ISO/SAE 21434 Edition 2 is under development in 2026; ISO/SAE 21434:2021 remains
  the current published baseline.
- ISO 26262 Edition 3 is under development in 2026; the published 2018 series
  remains the current normative baseline.
- A later published edition triggers a controlled impact review before changing
  the project baseline.

## Current implementation mapping

### CAN / CAN FD

Implementation:
- ecu::core::transport CAN types and contracts
- Linux SocketCAN adapter outside Core

Evidence already present:
- CAN contract tests
- SocketCAN codec tests
- hardware CAN/CAN-FD stage validation

Open before standards PASS:
- clause-level mapping against ISO 11898-1:2024
- physical-layer applicability mapping against ISO 11898-2:2026
- explicit boundary/error/interoperability cases

### ISO-TP / DoCAN

Implementation:
- portable IsoTpEndpoint
- transport-independent diagnostic adapter boundary

Evidence already present:
- ISO-TP Core tests
- sanitizer coverage
- architecture and portability gates

Open before standards PASS:
- clause-level mapping against ISO 15765-2:2024
- timing, flow-control, addressing, CAN FD and malformed-frame coverage matrix
- interoperability vectors against independent implementation/reference equipment

### UDS

Implementation:
- transport-independent UdsClient
- selected service builders/parsers

Evidence already present:
- UDS Core tests
- AUTOSAR DCM timing/session cross-check
- architecture gate preventing UDS -> ISO-TP coupling

Open before standards PASS:
- ISO 14229-1:2026 service/NRC requirement matrix
- ISO 14229-2:2021 session/timing primitive matrix
- ISO 14229-3:2022 UDSonCAN profile matrix
- explicit declaration of supported/unsupported services

### DoIP

Implementation:
- transport abstraction is ready for a DoIP implementation
- DoIP itself is not implemented

Required baseline:
- ISO 13400-2:2025
- ISO 13400-3:2016 where wired interface requirements apply
- ISO 14229-5:2022

Status: NOT_IMPLEMENTED

### J1939

Implementation:
- generic 29-bit identifier encode/decode and PGN semantics

Open:
- SAE J1939/21_202205 clause audit for current identifier/PGN code
- classical TP/ETP
- SAE J1939-22 CAN FD transport
- SAE J1939/81_202504 Address Claiming/network management
- SAE J1939-73_202609 diagnostics

Current status: IMPLEMENTED_PARTIAL / CLAUSE_AUDIT_REQUIRED

## Safety, security and updates

Architecture primitives such as resource ownership, lifecycle, cancellation,
trace/replay and fail-safe behavior are necessary engineering foundations, but
they are not by themselves evidence of ISO 26262 or ISO/SAE 21434 compliance.

Formal claims require the corresponding lifecycle work products, risk/safety
analysis, traceability, verification evidence and scope definition.

UNECE R155/R156 applicability depends on the eventual product and vehicle
approval scope. They are treated as regulatory design inputs, not automatically
as direct certification claims for the standalone workshop platform.

## Gate state

CORE_STANDARDS_BASELINE=PASS
CORE_STANDARDS_CONFORMANCE=BLOCKED

Reason:
The official standards baseline is now explicit, but existing CAN/ISO-TP/UDS/J1939
code has not yet completed clause-by-clause normative traceability and
independent interoperability evidence.

Production architecture completion is forbidden while
CORE_STANDARDS_CONFORMANCE=BLOCKED.

[executed on device: ecu (4a17dcf9-64bf-4337-8a46-7d88ad637c0f)]