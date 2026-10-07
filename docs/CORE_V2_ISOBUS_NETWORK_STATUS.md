# ECU Platform V2 — ISOBUS network foundation status

Date: 2026-10-07
Scope: AGRI / ISO 11783 working-set messaging
Status: **ENGINEERING FOUNDATION PASS / NORMATIVE CLAUSE AUDIT OPEN**

## Implemented scope

Core V2 contains a small, transport-independent ISOBUS working-set codec layer:

- Working Set Master (WSMSTR), PGN 65037 / 0xFE0D,
- Working Set Member (WSMEM), PGN 65036 / 0xFE0C,
- default priority 7,
- exact 8-byte Classic CAN payloads,
- WSMSTR member-count range 1..250,
- WSMSTR reserved bytes required to be 0xFF,
- WSMEM carries one 64-bit J1939/ISO 11783 NAME,
- source address must be a claimable address; NULL and Global are rejected,
- invalid NAME encodings fail closed.

The implementation reuses the Core V2 J1939 identifier and NAME codecs. It does
not own the physical CAN channel and does not create threads, timers or dynamic
storage.

## Explicitly outside this gate

This foundation does not implement:

- a working-set membership registry or lifecycle state machine,
- verification that all declared member messages were observed,
- member re-assignment or working-set dissolution semantics,
- Virtual Terminal object pools,
- Task Controller / process-data control,
- Tractor ECU control messages,
- TIM,
- Auxiliary Control,
- diagnostics,
- automatic actuation or any state-changing machine control.

Those capabilities remain separately gated.

## Evidence

Executable regression coverage verifies:

- WSMSTR wire encoding and round trip,
- member-count lower/upper boundaries and reserved values,
- exact DLC and reserved-byte rejection,
- invalid/null/global source-address rejection,
- WSMEM NAME encoding and round trip,
- invalid NAME rejection,
- PGN separation between WSMSTR and WSMEM.

The full Core V2 local validation passes Debug, Release, Generic non-Linux and
ASAN/UBSAN matrices with all current tests passing.

## Conformance statement

This is an engineering foundation, not a claim of ISO 11783 certification or
complete protocol conformance. The current reference baseline for implement
messages is ISO 11783-7:2022. Clause-level mapping against the licensed normative
text and independent ISOBUS interoperability evidence remain required before a
module-level standards PASS.

CORE_V2_ISOBUS_WORKING_SET_ENGINEERING=PASS
CORE_V2_ISOBUS_WORKING_SET_CLAUSE_AUDIT=OPEN
CORE_V2_ISOBUS_EXTERNAL_CONFORMANCE_CLAIMED=NO
