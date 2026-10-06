# ECU Platform V2 — Core architecture audit

Date: 2026-10-06

Status: **ACTIVE HARDENING**

This audit exists because Core is the long-lived product boundary of ECU Platform V2.
ECU-specific feature work is paused while mandatory shared contracts are hardened.

## Confirmed strengths before hardening

Already validated before this audit:

- platform-independent Core build,
- CAN/CAN-FD domain contract,
- Linux SocketCAN isolated in a platform adapter,
- portable ISO-TP state machine,
- portable UDS client,
- warning-as-error builds,
- portability source gate,
- sanitizer coverage for protocol stages.

## Critical issue found and corrected

### UDS was coupled directly to ISO-TP

Before Core Hardening v1:

```text
UdsClient -> IsoTpEndpoint
```

This violated the accepted requirement that the same UDS logic must work over
DoCAN/ISO-TP and DoIP.

Core Hardening introduces:

```text
UdsClient
   |
   v
IDiagnosticTransport
   |                 |
   v                 v
IsoTpDiagnostic   future DoIP
Transport         transport
```

UDS no longer imports or references ISO-TP.

An architecture gate now blocks reintroduction of this dependency.

## Core Hardening v1 additions

### Runtime contract primitives

- Command metadata and correlation IDs
- versioned state metadata
- event metadata and event sink
- cancellation flag
- lifecycle state machine
- lifecycle component contract

These define runtime semantics without choosing HTTP, WebSocket, systemd,
threads, or a process topology.

### Resource ownership

`ResourceManager` provides exclusive, generation-safe leases for logical:

- CAN channels
- Ethernet interfaces
- diagnostic channels
- actuators
- storage
- hardware I/O
- devices
- custom resources

It is platform-independent and prevents stale leases from releasing a newly
acquired resource.

### Trace / replay contracts

Core now defines:

- neutral trace categories/direction/severity,
- `ITraceSink`,
- `IReplaySource`.

No file format, database, or filesystem implementation is selected.

### Storage contract

`IKeyValueStore` defines portable binary read/write/erase operations.

No database or filesystem technology is selected.

### Generic J1939 identifier model

Core now has generic J1939 29-bit identifier and PGN handling, including correct
PDU1/PDU2 semantics.

This does not yet implement J1939 TP/ETP or Address Claiming.

## Mandatory architecture gates

The hardening gate blocks:

- UDS -> ISO-TP direct dependency,
- Core -> ECU-specific module imports,
- Core -> platform adapter imports,
- Linux/hardware-specific APIs in Core,
- Core-owned worker threads/sleeps,
- wall-clock use in Core protocol/runtime logic,
- filesystem implementation inside Core.

## Still open after v1 foundation

The following are still mandatory Core work, not optional feature backlog:

1. Full diagnostic networking / DoIP transport and networking abstraction.
2. J1939 TP/ETP and Address Claiming strategy.
3. Generic ECU/module registration contract.
4. Core command dispatcher and authoritative state aggregation.
5. Safety/fail-safe policy primitives.
6. Generic actuator runtime contract for deterministic time-critical control.
7. Concrete trace/replay implementation and CAN/DoIP simulation.
8. Persistent configuration implementation behind `IKeyValueStore`.
9. Device identity / attestation / licensing security contracts.
10. Core API contract presented to WebGUI/remote clients.
11. Multi-platform CI beyond source scanning.
12. Release/update/integrity lifecycle.

These items must be resolved before Core is declared production-architecture complete.

## Main branch boundary

Core Hardening work remains on `core-hardening/v1-foundation`.
No production `main` merge is authorized by this audit.
