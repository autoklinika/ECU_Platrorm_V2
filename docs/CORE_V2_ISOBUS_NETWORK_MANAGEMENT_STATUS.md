# ECU Platform V2 — ISOBUS network management status

Date: 2026-10-07
Scope: AGRI / ISO 11783 network management engineering foundation
Status: **ENGINEERING FOUNDATION PASS / NORMATIVE CLAUSE AUDIT OPEN**

## Baseline

The current ISO network-management reference is ISO 11783-5:2019, Edition 3.
ISO confirmed this edition in 2024 and it remains current.

The standard covers source-address management for control functions, association
of source addresses with functional identification, detection/reporting of
network-related errors and initialization of network-connected ECUs.

## Architecture

The ISOBUS layer does not implement a second Address Claim state machine.

Local source-address ownership and arbitration remain in the existing Core V2
SAE J1939 Address Claim / NetworkManager implementation. The ISOBUS network
management layer composes that implementation and adds bounded observation of
remote control functions.

This preserves one source of truth for:

- local NAME,
- preferred/fallback source address,
- Address Claimed request/response handling,
- conflict arbitration,
- Cannot Claim,
- optional Commanded Address,
- monotonic timing and deferred TX.

## Implemented engineering scope

### Strict Address Claimed codec

Core V2 now exposes one strict J1939 Address Claimed decoder reused by both the
J1939 state machine and ISOBUS network management.

It validates:

- exact eight-byte payload,
- PGN 60928 / 0xEE00,
- global destination,
- 64-bit NAME validity including the reserved NAME bit.

Malformed NAME data can no longer enter Address Claim arbitration.

### Remote control-function registry

A fixed-capacity registry tracks up to 64 observed remote control functions
without heap allocation.

For each observed NAME it records:

- 64-bit NAME,
- current claimed source address, or
- explicit Cannot Claim state.

The registry provides bounded lookup by NAME and by source address.

### Network consistency handling

The layer explicitly reports:

- new remote control functions,
- address changes for an existing NAME,
- Cannot Claim reports,
- two different NAMEs claiming the same source address,
- malformed Address Claimed frames,
- fixed-capacity exhaustion.

An address lookup with multiple claimant NAMEs returns an explicit ambiguous
result instead of selecting a winner outside the J1939 arbitration state
machines.

When a NAME reports Cannot Claim, its previous source-address association is
removed so consumers cannot continue using a stale address.

Registry capacity exhaustion is latched as incomplete network topology. It does
not disable the local J1939 Address Claim safety path: local claim arbitration
and mandatory Address Claimed responses continue to operate.

## Explicitly outside this gate

This engineering foundation does not claim implementation of every normative
ISO 11783-5 procedure.

Still open are:

- clause-by-clause ISO 11783-5:2019 requirement mapping,
- complete normative ECU initialization behavior,
- every network-error reporting rule,
- independent ISOBUS interoperability evidence,
- conformance/certification testing.

Higher-layer functions are also outside this gate:

- Virtual Terminal,
- Task Controller,
- TIM,
- Auxiliary Control,
- process-data actuation,
- ISOBUS diagnostics,
- machine-control policy.

## Executable evidence

Regression coverage includes:

- strict Address Claimed decode,
- invalid NAME reserved-bit rejection before J1939 arbitration,
- NAME-to-address and address-to-NAME lookup,
- repeated-claim idempotence,
- address migration,
- Cannot Claim transition without stale address,
- conflicting address ambiguity and resolution,
- malformed-frame rejection,
- unrelated ISOBUS traffic isolation,
- fixed registry capacity and explicit overflow,
- composition with the existing J1939 local NetworkManager,
- local fallback-address arbitration while the remote registry observes the
  competing control function,
- continued local Address Claimed response after registry overflow.

The complete local Core V2 validation passes Debug, Release, Generic non-Linux
and ASAN/UBSAN matrices with all current tests passing.

CORE_V2_ISOBUS_NETWORK_MANAGEMENT_ENGINEERING=PASS
CORE_V2_ISOBUS_NETWORK_MANAGEMENT_CLAUSE_AUDIT=OPEN
CORE_V2_ISOBUS_NETWORK_MANAGEMENT_EXTERNAL_CONFORMANCE=NO
