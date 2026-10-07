# ECU Platform V2 — J1939 Network + TP checkpoint

Date: 2026-10-07

Status: **TECHNICAL GATE PASS / SAE CLAUSE AUDIT REQUIRED**

## Scope

This checkpoint builds the first protocol layer above the portable Core V2 CAN
runtime for the product domains:

- TRUCK,
- AGRI,
- OHV.

It does not broaden product scope to passenger-car-specific behavior.

## Implemented

### J1939 identifier / PGN foundation

- 29-bit Classical Extended Frame Format handling,
- PDU1/PDU2 distinction,
- destination-specific PDU1 addressing,
- PDU2 group-extension PGN construction,
- reserved identifier-bit rejection,
- bounded classic data-frame builder.

### J1939 NAME

- complete 64-bit NAME field encoding/decoding,
- little-endian Address Claimed payload encoding,
- NAME-priority comparison (lower numeric NAME wins).

### Address Claim subset

- PGN 60928 Address Claimed / Cannot Claim,
- generic PGN 59904 Request codec with exact 3-byte little-endian requested PGN,
- canonical requested-PGN validation for global and destination-specific requests,
- Request for Address Claimed built/decoded through the shared Request codec,
- PGN 65240 Commanded Address after Classical TP reassembly, opt-in by policy,
- Commanded Address targeting by 64-bit NAME with a 9-byte NAME + new-SA payload,
- preferred address,
- bounded alternative address list for arbitrary-address-capable ECUs,
- 250 ms stabilization window for addresses 128..247,
- immediate activation for addresses outside that window,
- conflict arbitration by NAME,
- Cannot Claim with NULL address 0xFE,
- duplicate-NAME fail-closed behavior,
- injected bounded delay for Cannot Claim response,
- monotonic time-domain and uncertainty checks,
- global address 0xFF rejected as a source address,
- explicit conflict accounting for both local-win and local-loss arbitration.

Commanded Address acceptance is disabled by default and must be enabled by the
owning product/module policy. The remaining J1939/81 network-management
behaviors outside this declared subset are not claimed by this checkpoint.

### Classical J1939 Transport Protocol

Receiver:
- TP.CM PGN 0xEC00,
- TP.DT PGN 0xEB00,
- BAM,
- RTS/CTS,
- EOM ACK,
- Abort,
- 7 data bytes per DT packet,
- maximum 255 packets / 1785 bytes,
- bounded BAM and peer sessions,
- one simultaneous BAM RX + one peer RX session,
- FF-padding validation,
- bounded completed-message queue,
- resource/busy/timeout Abort paths.

Transmitter:
- BAM pacing with configured interval constrained to 50..200 ms,
- RTS/CTS flow control,
- CTS(0) hold behavior,
- block transmission,
- EOM ACK completion,
- peer timeout Abort,
- one simultaneous BAM TX + one peer TX session,
- bounded deferred TX queue,
- fail-closed time-domain handling.

## Core integration contract

All J1939 state machines are platform-independent and use no OS, driver SDK,
thread, sleep, dynamic container or filesystem API.

Protocol callbacks never call `CanBusRuntime::send()` reentrantly. Control/data
frames are queued in fixed-capacity deferred-TX queues and must be drained by
the owning executor after `CanBusRuntime::poll()` returns.

The authoritative protocol RX path must not receive an echo of a frame accepted
for TX by the same driver instance. Core RX records intentionally have no
local/remote-origin bit; admitting own-TX echo would make Address Claimed
arbitration unable to distinguish the local ECU from a remote ECU.

`ICanFrameSink` is explicitly non-owning. Its destructor is protected and
non-virtual because the runtime never deletes a sink through the interface;
concrete sinks must outlive their subscription. This also removes C++ deleting
destructor dependencies from deterministic Core.

## Verification

Executable gates:

- `ecu.core_v2.j1939.network`
- `ecu.core_v2.j1939.request`
- `ecu.core_v2.j1939.tp`
- `ecu.core_v2.j1939.tp_tx`
- `ecu.core_v2.j1939.tp_e2e`

The aggregate `ecu_core_v2_tests` build target ensures all Core V2 protocol
executables are built before CTest on every supported toolchain.

Local acceptance evidence on 2026-10-07:

- Debug: aggregate Core V2 protocol matrix PASS,
- Release: aggregate Core V2 protocol matrix PASS,
- Generic non-Linux CMake system: aggregate matrix PASS,
- ASAN + UBSAN (excluding vptr because Core is intentionally `-fno-rtti`):
  aggregate matrix PASS,
- external runtime-symbol gate: PASS,
- dynamic-static-initialization gate: PASS,
- architecture/portability negative gates: PASS.

## Normative/reference status

Normative baselines:
- SAE J1939/21_202205 — Data Link Layer,
- SAE J1939/81_202504 — Network Management.

Public implementation cross-check:
- Linux kernel J1939 stack documentation and transport implementation.

The full licensed SAE normative text is not present in this repository.
Therefore this checkpoint does **not** claim formal SAE J1939 conformance.
A clause-by-clause audit and independent interoperability evidence remain
mandatory before module-level standards PASS.

## Adjacent layers

ISO 11783 / ISOBUS ETP is now implemented as a separate streaming transport
module and has its own technical gate in `CORE_V2_ISOBUS_ETP_STATUS.md`.

J1939-73 diagnostics now has a separate read-only DM1/DM2 foundation and its
own module gate in `CORE_V2_J1939_DIAGNOSTICS_STATUS.md`.

J1939_NETWORK_TECHNICAL_GATE=PASS
J1939_COMMANDED_ADDRESS_TECHNICAL_GATE=PASS
J1939_TP_TECHNICAL_GATE=PASS
J1939_STANDARDS_CONFORMANCE=CLAUSE_AUDIT_REQUIRED
J1939_ETP_STATUS=TECHNICAL_PASS_SEPARATE_ISOBUS_MODULE
