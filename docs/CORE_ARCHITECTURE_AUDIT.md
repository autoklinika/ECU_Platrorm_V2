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

## Core Hardening v2 additions

### Command dispatch and policy boundary

Core now owns an API-neutral command dispatcher with:

- unique command-type registration,
- correlation metadata,
- policy evaluation before handler execution,
- explicit deny / confirmation-required outcomes.

This keeps authorization and safety decisions inside Core rather than the WebGUI.

### Authoritative state registry and Core facade

Core now provides:

- typed state-provider registration,
- revisioned state snapshots,
- `CoreFacade::submit()`,
- `CoreFacade::snapshot()`.

The future HTTP/WebSocket backend can therefore remain a transport adapter over
Core semantics instead of owning application logic.

### Module registry

`ModuleRegistry` provides stable module identity, capability metadata and
lifecycle ownership without selecting a static-plugin or dynamic-plugin loading
mechanism.

### Device registry

`DeviceRegistry` provides logical device identity, class and capability masks.
No vendor, VID/PID, device path or OS-specific detail is part of the contract.

### Actuator and safety foundations

Core now requires actuators to expose `safe_stop()` and provides a monotonic
`SafetyWatchdog` primitive for fail-safe timing.

No actuator-specific control algorithm is embedded in generic Core.

### Networking contracts for future DoIP

Core now defines platform-neutral stream/datagram channel contracts and IP
endpoint types. They do not expose Linux file descriptors or BSD socket APIs.

### Security extension points

Core now defines opaque device identity/attestation and authorization contracts
without selecting TPM, Secure Element, key algorithm, PKI or licensing policy.

## Core Hardening v3 additions

### Event bus

Core now has a bounded subscription model with monotonic timestamps and
Core-assigned event sequence numbers. Event sinks are invoked without holding
the registry lock.

### Flight recorder / replay

Core now includes a bounded, fixed-capacity in-memory flight recorder that:

- assigns trace sequence numbers,
- records truncation explicitly,
- supports deterministic replay snapshots,
- performs no heap allocation,
- has no filesystem or database dependency.

Persistent export remains an adapter responsibility.

### Simulated CAN

`SimulatedCanInterface` implements the frozen `ICanInterface` contract with
bounded RX/TX queues. It supports integration testing without Linux, SocketCAN or
physical hardware.

### Core-only build graph

CMake can now explicitly build and test:

```text
ECU_BUILD_SAC_MODULE=OFF
ECU_BUILD_LINUX_SOCKETCAN=OFF
```

The validation gate verifies that neither product ECU modules nor Linux platform
targets are present in this graph.

## Still open after v3 foundation

The following are still mandatory Core work, not optional feature backlog:

1. Full DoIP protocol implementation over the new networking contracts.
2. J1939 TP/ETP and Address Claiming strategy.
3. Persistent configuration implementation behind `IKeyValueStore`.
4. Product security design: Root of Trust, attestation format, licensing and key lifecycle.
5. Generic deterministic actuator scheduler/interlock policy beyond the base contract.
6. Persistent trace export and DoIP replay adapters.
7. Multi-platform CI beyond the current core-only graph/source gates.
8. Release/update/integrity lifecycle.
9. Final process topology and deployment lifecycle.

These items must be resolved before Core is declared production-architecture complete.

## Main branch boundary

Core Hardening work remains on `core-hardening/v1-foundation`.
No production `main` merge is authorized by this audit.
