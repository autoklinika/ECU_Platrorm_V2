# ECU Platform V2 — Standards Traceability Baseline

Date: 2026-10-06
Status: ACTIVE / MANDATORY FOR CORE HARDENING

A passing unit, integration, sanitizer, portability or architecture test is not
by itself proof of standards conformance.

## Product applicability scope

ECU Platform V2 targets only:
- heavy-duty road trucks,
- agricultural machinery (AGRI),
- off-highway machinery (OHV).

Passenger cars are explicitly out of product scope. Standards common to road
vehicles remain applicable only where they are technically or legally relevant
to truck/AGRI/OHV.

Priority domain split:
- Heavy-duty truck: SAE J1939, ISO 11992, ISO 27145/WWH-OBD, UDS/DoCAN/DoIP.
- AGRI: SAE J1939 plus ISO 11783/ISOBUS; ISO 25119 for safety-related control
  systems where applicable.
- OHV: SAE J1939 plus OEM/industry protocols; ISO 19014 where the machine is
  earth-moving machinery within ISO 6165 scope.

ISO 26262 and UNECE R155/R156 are not generic AGRI/OHV requirements. They are
kept only for the applicable on-road truck/vehicle scope.

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
| J1939 top-level heavy-duty network | SAE J1939_202603 | BASELINE_VERIFIED |
| J1939 vehicle application layer | SAE J1939/71_202502 | CLAUSE_AUDIT_REQUIRED |
| Truck/trailer diagnostic communication | ISO 11992-4:2023 | NOT_IMPLEMENTED |
| Truck/trailer brakes/running gear | ISO 11992-2:2023 (successor DIS under development in 2026) | NOT_IMPLEMENTED |
| WWH-OBD vehicle/tester connection | ISO 27145-4:2016 | NOT_IMPLEMENTED |
| WWH-OBD external test equipment | ISO 27145-6:2023 | NOT_IMPLEMENTED |
| AGRI/ISOBUS application, transport and network | ISO 11783-3:2026 | NOT_IMPLEMENTED |
| AGRI/ISOBUS diagnostic services | ISO 11783-12:2019 (Edition 4 FDIS under development in 2026) | NOT_IMPLEMENTED |
| AGRI safety-related controls | ISO 25119-1:2018 and applicable ISO 25119 series | SCOPE_ASSESSMENT_REQUIRED |
| OHV earth-moving functional safety | ISO 19014-1:2018 and applicable ISO 19014 series; Edition 2 under publication in 2026 | SCOPE_ASSESSMENT_REQUIRED |
| Cybersecurity engineering | ISO/SAE 21434:2021 | BASELINE_VERIFIED / PROCESS_NOT_CLOSED |
| Vehicle cybersecurity regulation — applicable on-road truck scope | UN Regulation No. 155 | SCOPE_ASSESSMENT_REQUIRED |
| Software update engineering | ISO 24089:2023 + Amd 1:2024 | NOT_IMPLEMENTED |
| Vehicle software-update regulation — applicable on-road truck scope | UN Regulation No. 156 | SCOPE_ASSESSMENT_REQUIRED |
| Functional safety — on-road truck scope only | ISO 26262:2018 series | SCOPE_ASSESSMENT_REQUIRED |

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

## Heavy-duty / AGRI / OHV profile

### Heavy-duty trucks

Primary standards and families:
- SAE J1939 family for in-vehicle heavy-duty communications,
- ISO 11992 for towing/towed vehicle communication,
- ISO 27145 for WWH-OBD external diagnostic equipment where applicable,
- UDS / ISO-TP / DoIP where used by the target ECU architecture.

### AGRI

Primary standards and families:
- ISO 11783 / ISOBUS,
- SAE J1939 where used below or alongside ISOBUS,
- ISO 25119 for safety-related control systems where applicable.

ISO 11783-3:2026 maps its application/transport/network layers to CAN and bases
transport/network behavior on SAE J1939-21. ISO 11783-12 defines the network
diagnostic system.

### OHV

Primary standards and families:
- SAE J1939 for construction/off-highway networks,
- UDS / ISO-TP / DoIP where OEMs use them,
- ISO 19014 where the target is earth-moving machinery within ISO 6165 scope.

Other OHV sectors can require additional sector-specific standards. They must
be added to this matrix before a corresponding module can receive a standards
conformance PASS.

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
