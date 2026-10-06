# ECU Platform V2 — Standards Traceability Baseline

Date: 2026-10-06
Status: ACTIVE / MANDATORY FOR CORE HARDENING

A passing unit, integration, sanitizer, portability or architecture test is not
by itself proof of standards conformance.

## Mandatory conformance policy

For every protocol or safety/security-relevant Core capability we require:

1. exact publisher and standard/recommended-practice edition,
2. declared implementation scope,
3. requirement/clause-to-code traceability,
4. requirement/clause-to-test traceability,
5. negative, boundary and interoperability evidence,
6. recorded conformance status.

Status levels:
- BASELINE_VERIFIED — current official publication/edition verified.
- IMPLEMENTED_PARTIAL — code exists, complete normative audit not closed.
- CLAUSE_AUDIT_REQUIRED — full normative text must be checked requirement-by-requirement.
- CONFORMANCE_TESTED — mapped applicable requirements have executable evidence.
- PASS — all applicable requirements in declared scope are closed.
- N/A — outside declared product scope, with rationale.

## Current normative baseline

| Domain | Normative/reference baseline | Current project status |
|---|---|---|
| CAN / CAN FD data link | ISO 11898-1:2024 | IMPLEMENTED_PARTIAL / CLAUSE_AUDIT_REQUIRED |
| High-speed CAN physical layer | ISO 11898-2:2026 | BASELINE_VERIFIED / CLAUSE_AUDIT_REQUIRED |
| DoCAN / ISO-TP | ISO 15765-2:2024 | IMPLEMENTED_PARTIAL / CLAUSE_AUDIT_REQUIRED |
| UDS application layer | ISO 14229-1:2026 | IMPLEMENTED_PARTIAL / CLAUSE_AUDIT_REQUIRED |
| UDS session layer | ISO 14229-2:2021 | IMPLEMENTED_PARTIAL / CLAUSE_AUDIT_REQUIRED |
| UDS on CAN | ISO 14229-3:2022 | IMPLEMENTED_PARTIAL / CLAUSE_AUDIT_REQUIRED |
| AUTOSAR diagnostic cross-check | AUTOSAR Classic Platform DCM R24-11 or newer verified release | REFERENCE_ONLY |
| DoIP transport/network | ISO 13400-2:2025 | NOT_IMPLEMENTED |
| DoIP wired interface | ISO 13400-3:2016 | NOT_IMPLEMENTED |
| UDS on IP | ISO 14229-5:2022 | NOT_IMPLEMENTED |
| J1939 Classical data link / transport | SAE J1939/21_202205 | IMPLEMENTED_PARTIAL / CLAUSE_AUDIT_REQUIRED |
| J1939 CAN FD | SAE J1939-22_202209 | NOT_IMPLEMENTED |
| J1939 network management | SAE J1939/81_202504 | NOT_IMPLEMENTED |
| J1939 diagnostics | SAE J1939-73_202609 | NOT_IMPLEMENTED |
| Cybersecurity engineering | ISO/SAE 21434:2021 | BASELINE_VERIFIED / PROCESS_NOT_CLOSED |
| Vehicle cybersecurity regulation | UN Regulation No. 155 | SCOPE_ASSESSMENT_REQUIRED |
| Software update engineering | ISO 24089:2023 + Amd 1:2024 | NOT_IMPLEMENTED |
| Vehicle software-update regulation | UN Regulation No. 156 | SCOPE_ASSESSMENT_REQUIRED |
| Functional safety | ISO 26262:2018 series | SCOPE_ASSESSMENT_REQUIRED |

Notes:
- ISO 15765-2 Edition 5 is under development in 2026; ISO 15765-2:2024 remains
  the published baseline until replacement is published.
- ISO/SAE 21434 Edition 2 is under development in 2026; ISO/SAE 21434:2021
  remains the published baseline.
- ISO 26262 Edition 3 is under development in 2026; the 2018 series remains
  the published baseline.
- A later published edition triggers an impact review before changing baseline.

## Current implementation mapping

### CAN / CAN FD
Implemented:
- portable CAN/CAN-FD contracts in Core,
- Linux SocketCAN adapter outside Core.

Before PASS:
- clause mapping to ISO 11898-1:2024,
- applicable hardware/physical mapping to ISO 11898-2:2026,
- explicit error/boundary/interoperability evidence.

### ISO-TP / DoCAN
Implemented:
- portable ISO-TP state machine,
- diagnostic transport abstraction.

Before PASS:
- clause mapping to ISO 15765-2:2024,
- timing, flow-control, addressing, CAN FD and malformed-frame matrix,
- independent interoperability vectors.

### UDS
Implemented:
- transport-independent UDS client,
- selected service builders/parsers.

Before PASS:
- ISO 14229-1:2026 service/NRC matrix,
- ISO 14229-2:2021 session/timing matrix,
- ISO 14229-3:2022 UDSonCAN profile matrix,
- explicit supported/unsupported service declaration.

AUTOSAR may be used as an implementation cross-check but never replaces ISO.

### DoIP
Not implemented yet.
Mandatory baseline:
- ISO 13400-2:2025,
- ISO 13400-3:2016 where wired interface requirements apply,
- ISO 14229-5:2022.

### J1939
Implemented:
- generic 29-bit identifier encode/decode and PGN semantics.

Before PASS:
- clause audit against SAE J1939/21_202205,
- TP/ETP,
- CAN FD rules per SAE J1939-22_202209,
- Address Claiming/network management per SAE J1939/81_202504,
- diagnostics per SAE J1939-73_202609 when implemented.

## Safety, security and updates

Resource ownership, lifecycle, cancellation, trace/replay and fail-safe
primitives are engineering foundations; they are not by themselves evidence of
ISO 26262 or ISO/SAE 21434 compliance.

Formal claims require applicable lifecycle work products, scope definition,
risk/safety analysis, traceability and verification evidence.

UN R155/R156 applicability depends on eventual product and vehicle-approval
scope. They are regulatory design inputs, not automatic certification claims
for a standalone workshop platform.

## Gate state

CORE_STANDARDS_BASELINE=PASS
CORE_STANDARDS_CONFORMANCE=BLOCKED

Reason:
The official baseline is explicit, but existing CAN/ISO-TP/UDS/J1939 code has
not yet completed clause-by-clause normative traceability and independent
interoperability evidence.

Production architecture completion is forbidden while
CORE_STANDARDS_CONFORMANCE=BLOCKED.
