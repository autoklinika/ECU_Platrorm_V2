# ECU Platform V2 — Runtime foundation status

Date: 2026-10-07
Scope: DUT-neutral Core V2 runtime foundation
Status: **ENGINEERING FOUNDATION PASS / FINAL CI PENDING**

## Purpose

This layer provides the stable bounded runtime semantics required by Bench Session
and DUT profiles without introducing OS threads, mutex ownership or dynamic topology.

Core V2 remains single-executor by contract. The platform/application scheduler
serializes calls into these runtime objects.

## Implemented contracts

### Lifecycle

`LifecycleStateMachine` provides explicit stopped -> starting -> running -> stopping
-> stopped transitions, fault latching and explicit fault reset.

### Generation-safe resource ownership

`ResourceManager` owns fixed-capacity leases for resources including CAN channels,
Ethernet interfaces, diagnostic channels, actuators, hardware I/O, power domains,
DUTs and storage.

Every lease carries owner + generation. A stale lease cannot release a later
generation of the same resource.

### Command dispatch

`CommandDispatcher` provides:

- fixed-capacity command handlers,
- immutable topology after `freeze_configuration()`,
- explicit command policy before handler execution,
- caller-owned payload views,
- bounded policy/handler execution contracts,
- explicit `busy` on reentrant dispatch instead of blocking.

### Authoritative state

`StateRegistry` provides fixed registered providers, revisioned state headers and
caller-buffered snapshots. Runtime registration after freeze is forbidden.

### Event sequencing

`EventBus` assigns Core-owned monotonic sequence numbers and monotonic timestamps,
supports exact-type and wildcard subscriptions, declares callback WCET bounds,
rejects clock faults and returns `busy` on reentrant publish.

### Module topology

`ModuleRegistry` holds fixed stable module identities and lifecycle components.
Runtime add/remove mutation is deliberately excluded after freeze.

### DUT topology

`DutRegistry` stores validated DUT descriptors by value and returns stable
generation-bearing handles. A reused profile ID with different semantics is an
explicit conflict.

### Cancellation

`CancellationSource` uses operation generations rather than a reusable boolean.
A cancellation token from an old operation cannot cancel a later Bench Session
operation.

## Shared-bus DUT-neutral proof

An executable integration test runs, on one authoritative `CanBusRuntime`:

- J1939 NetworkManager,
- ISO-TP endpoint,
- proprietary raw cyclic actuator runtime.

J1939 and ISO-TP subscribe through the shared RX router. All J1939, ISO-TP and
actuator TX passes through `CanBusRuntime::send()`. Mixed RX is delivered only to
matching subscribers. Actuator safe-stop does not disturb the protocol states.

This proves that ECU-style and actuator-style workloads fit the same Core
transport/runtime contract without direct physical-driver access.

## Explicit boundaries

This foundation does not create OS threads, worker pools, GUI timers or platform
schedulers. It does not prescribe how Linux, Windows or a future VCI schedules the
single executor.

Bench Session remains an upper layer. DUT-specific command encoding, checksums,
counter logic and engineering scaling remain profile responsibilities.

DoIP/network stream adapters, persistent storage, product security, UI/API and
separate capture/replay hardware are not required to mutate these runtime
contracts and remain separately gated.

## Engineering evidence

Local validation must pass:

- Debug,
- Release,
- Generic non-Linux CMake system,
- ASAN/UBSAN,
- architecture gate,
- portability gate,
- scoped conformance gate,
- external-runtime-symbol gate,
- dynamic-static-initialization gate.

Cross-platform GitHub CI remains the final gate before this layer is marked closed.

CORE_V2_RUNTIME_FOUNDATION_ENGINEERING=PASS
CORE_V2_RUNTIME_TOPOLOGY=FROZEN_AFTER_CONFIGURATION
CORE_V2_RUNTIME_EXECUTION_MODEL=SINGLE_EXECUTOR
CORE_V2_SHARED_BUS_DUT_NEUTRAL_PROOF=PASS
