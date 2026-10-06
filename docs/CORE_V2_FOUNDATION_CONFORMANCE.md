# ECU Platform V2 — Core V2 foundation conformance evidence

Date: 2026-10-07  
Status: **PASS — declared foundation scope only**

## Decision

The standards/conformance gate is closed for the **portable Core V2 foundation**
used by **TRUCK, AGRI and OHV**.

This is an engineering conformance claim for the explicitly declared foundation
scope. It is **not** an external certification and it does not claim that future
protocol modules already conform to standards they have not yet implemented.

The machine-readable source of truth is:

- `docs/CORE_V2_FOUNDATION_CONFORMANCE.json`

The fail-closed validator is:

- `scripts/check_core_v2_conformance.py`

## Product scope

Exactly three machine domains are allowed:

- TRUCK / heavy-duty road vehicles,
- AGRI / agricultural and forestry machinery,
- OHV / off-highway machinery.

Passenger-car-only scope is excluded.

## Normative baseline relevant to the foundation

### ISO 11898-1:2024

Current published CAN data-link/physical-coding baseline. Core V2 deliberately
models **CAN Classic + CAN FD** software-facing frame/link contracts and does not
claim CAN XL support.

Official source:
https://www.iso.org/standard/86384.html

### ISO 11898-2:2026

Current published high-speed CAN PMA baseline. Electrical/transceiver
conformance is outside portable Core. It belongs to the concrete hardware,
transceiver and platform-adapter qualification.

Official source:
https://www.iso.org/standard/90697.html

### SAE J1939/21_202205

Applicable across heavy-duty on-road/off-road, construction and agricultural
equipment. The Core V2 claim is intentionally narrow: it provides the Classic
Extended Frame Format / 29-bit CAN substrate needed by later J1939 modules.
PGN semantics, Address Claiming, TP/ETP and J1939 diagnostics remain separate
module conformance work.

Official source:
https://saemobilus.sae.org/standards/j193921_202205-data-link-layer

### ISO 11783-3:2026

Current AGRI/forestry application/transport/network baseline maps to CAN and is
based in part on SAE J1939-21 behavior. Core V2 provides the common CAN substrate
and first-class AGRI product scope; ISO 11783 higher layers are not claimed by
the foundation.

Official source:
https://www.iso.org/standard/89949.html

### ISO 11783-12:2019

Current published ISOBUS diagnostics-services baseline at the time of this
review. It is explicitly module-gated and not part of the foundation claim.

Official source:
https://www.iso.org/standard/71184.html

### ISO 15765-2:2024

Current published DoCAN transport/network baseline. ISO-TP is explicitly
module-gated and not part of the foundation claim.

Official source:
https://www.iso.org/standard/84211.html

### ISO 25119-1:2018 and ISO 19014-1:2018

These remain important safety baselines for applicable AGRI and OHV products.
A generic monotonic clock/watchdog is only an enabling primitive. The foundation
does not convert that primitive into a functional-safety lifecycle or product
certification claim.

Official sources:
https://www.iso.org/standard/69025.html
https://www.iso.org/standard/70715.html

## Evidence model

Every **implemented** foundation requirement must have:

1. an explicit requirement ID,
2. product-domain applicability,
3. normative references where applicable,
4. code markers,
5. executable test/gate markers,
6. status PASS.

Every **boundary** requirement must prove that responsibility is deliberately
kept outside Core rather than silently treated as conformant.

CI validates that every referenced file and marker still exists. Deleting or
renaming implementation/tests without updating the conformance evidence fails
the gate.

## Closed foundation evidence

The manifest currently covers:

- exact TRUCK/AGRI/OHV product scope,
- 11-bit and 29-bit identifier bounds,
- CAN Classic and CAN FD frame validity,
- driver capability gating and listen-only behavior,
- shared-bus routing,
- exclusive physical-channel ownership,
- transport-session isolation,
- fail-closed transport fault handling,
- RX time-domain/uncertainty/ordering provenance,
- observable RX loss,
- conservative watchdog timing,
- finite runtime execution budgets,
- no Core-owned runtime heap allocation,
- multi-platform isolation/portability,
- J1939 foundation boundary,
- AGRI/ISO 11783 foundation boundary,
- physical CAN layer boundary,
- AGRI/OHV functional-safety claim boundary.

## Module-gated work after foundation

No foundation PASS is inherited automatically by these modules:

- J1939 PGN model,
- J1939 Address Claiming,
- J1939 TP/ETP,
- J1939 diagnostics / DM services,
- ISO 11783 / ISOBUS application, transport, network and diagnostics,
- ISO-TP / DoCAN,
- UDS,
- DoIP,
- ISO 11992,
- WWH-OBD,
- safety lifecycle work products under ISO 25119 / ISO 19014,
- cybersecurity/update lifecycle claims.

Each such module requires its own normative scope, requirement mapping,
negative/boundary tests and interoperability evidence before receiving PASS.

## Gate result

`CORE_V2_FOUNDATION_STANDARDS_CONFORMANCE=PASS`

`CORE_PROTOCOL_CONFORMANCE_POLICY=MODULE_GATED`

No external certification is claimed.
