# ECU Platform V2 — Core V2 Architecture Baseline

Date: 2026-10-07

Status: **FOUNDATION / TECHNICAL + SCOPED CONFORMANCE GATES PASS**

Reference baseline preserved at:

- branch: `core-hardening/v1-foundation`
- commit: `e3a05efdf9cf452ff825e1c4be2c93fb6539eeef`

Core V2 is a clean foundation rebuild. The previous Core remains a verified
reference source; its contracts are not automatically carried forward.

## 1. Product scope invariant

Core V2 exists only for:

- TRUCK / heavy-duty road vehicles,
- AGRI / agricultural machinery,
- OHV / off-highway machinery.

Passenger-car-only concepts are out of scope.

First-class protocol families:

- SAE J1939,
- ISO 11783 / ISOBUS,
- ISO-TP / DoCAN where used by target ECUs,
- UDS where used by target ECUs,
- DoIP where used by target ECUs,
- ISO 11992 and WWH-OBD where applicable to heavy-duty trucks.

## 2. Architectural invariants

1. Core is independent of OS, board, driver SDK and GUI.
2. A physical CAN channel has one authoritative runtime owner.
3. Protocols never call a physical CAN driver directly.
4. One central CAN bus runtime receives frames and dispatches them to bounded
   subscribers.
5. J1939, ISOBUS, ISO-TP, scanner and future protocol consumers may observe the
   same bus concurrently without stealing frames from each other.
6. Runtime registration is configuration-time only. The graph is frozen before
   RUNNING.
7. Hot-plug is represented as state on a stable logical slot; it does not imply
   unsafe removal of registered objects while callbacks can execute.
8. Critical Core paths use bounded storage and no unbounded heap allocation.
9. Core creates no worker threads and performs no sleeps. Scheduling belongs to
   the host/runtime adapter.
10. All time-dependent Core behavior uses an injected monotonic clock.
11. Safety deadline expiry is fail-closed. A late kick cannot revive an expired
    watchdog.
12. GUI/API clients issue high-level commands only. They never own diagnostic or
    actuator timing.
13. Standards conformance is never inferred from ordinary unit-test success.
14. `main` merge remains a separate user-authorized decision.
15. Runtime-path adapter calls and protocol callbacks are non-blocking and have
    declared finite execution bounds; a `try_*` name alone is not evidence.
16. Safety time uses a healthy named clock domain that advances across
    suspend/resume and reports discontinuity/unavailability explicitly.
17. Core public C++ structures are source-level contracts, not wire/IPC or
    cross-toolchain binary ABI records; serialization uses explicit encodings.

## 3. Layer model

### L0 — Core primitives

Responsibilities:

- fixed-width identifiers,
- bounded views/records,
- monotonic time,
- result/status types,
- product-domain applicability metadata.

Forbidden:

- OS APIs,
- filesystem,
- sockets,
- hardware identifiers,
- UI concepts.

### L1 — Platform contracts

Examples:

- CAN driver,
- network stream/datagram driver,
- persistent storage,
- secure identity / root-of-trust adapter,
- monotonic clock,
- hardware I/O adapters.

These are contracts only. Linux/Windows/MCU implementations live outside Core.
A platform implementation must also provide one authoritative physical-resource
arbiter for every CAN channel identity, including when multiple adapter wrapper
objects reference the same hardware channel.

### L2 — Link ownership and routing

The physical link is owned here.

For CAN:

```text
ICanDriver
    |
    v
CanBusRuntime
    |
    +--> bounded frame router --> J1939
    |                       +--> ISOBUS
    |                       +--> ISO-TP
    |                       +--> scanner/trace
    |
    +--> centralized TX path
```

A protocol never invokes `try_receive()` on the driver.

### L3 — Network / transport protocols

Examples:

- ISO-TP / DoCAN,
- J1939 TP/ETP,
- J1939 Address Claiming / network management,
- ISO 11783 transport/network services,
- DoIP.

These consume link-runtime ports and frame/event streams, not platform drivers.

### L4 — Diagnostic/application protocols

Examples:

- UDS,
- J1939 diagnostics,
- ISOBUS diagnostics,
- WWH-OBD mappings,
- manufacturer/OEM protocol services.

UDS remains transport-independent.

### L5 — ECU and machine modules

Contains knowledge specific to:

- ECU family,
- machine/vehicle family,
- DIDs/PIDs/SPNs/PGNs,
- diagnostic procedures,
- actuator procedures,
- OEM extensions.

Generic Core must not depend on any one module.

### L6 — Runtime semantics

Responsibilities:

- command dispatch,
- authoritative state,
- event sequencing,
- stable module registration,
- resource ownership,
- lifecycle,
- cancellation.

Runtime topology is configured before RUNNING and then frozen.

### L7 — Safety and deterministic actuation

Responsibilities:

- interlocks,
- deadlines,
- safe-stop semantics,
- deterministic scheduling contracts,
- ownership of actuator execution.

A generic watchdog is only a primitive. Safety claims require domain-specific
analysis and evidence.

### L8 — API facade

The Core API exposes commands, state and events to adapters.

HTTP/WebSocket/WebGUI are adapters and do not own product logic.

## 4. Concurrency model

Core V2 does not assume one particular OS thread model.

The baseline contract is:

- configuration calls happen before RUNNING,
- frame dispatch for one bus is serialized by its owning executor,
- `CanBusRuntime` and `DeadlineWatchdog` are single-executor objects and are
  not internally thread-safe; all calls are serialized by their owner,
- protocol callbacks are non-blocking, allocation-free on the runtime path and
  carry a declared maximum callback duration,
- callback registration is immutable while RUNNING,
- registered sinks and the driver MUST outlive the CanBusRuntime object itself,
  including stopped intervals and restarts; no unregister/hot-removal exists,
- physical-driver lease ownership is distinct from physical open state; once
  acquired, a runtime retains its exclusive lease across both stopped and
  faulted states. Normal stop closes only the transport session; explicit
  recovery from fault or terminal destruction releases the physical-channel
  lease. Another runtime therefore cannot acquire the channel merely because
  the adapter has transitioned itself to closed,
- lease arbitration is by stable physical-channel identity through one
  authoritative arbiter shared by all wrappers for that physical channel,
- RX callback frames are borrowed only until the callback returns; consumers
  copy any deferred data into their own bounded storage,
- TX frames are borrowed only during `try_send()`; the adapter consumes or
  copies required data before returning. `ok` means exactly-once acceptance
  into a bounded FIFO submission path, not physical transmission; `would_block`
  means no acceptance,
- runtime-path driver calls never sleep, wait on an external event or blocking
  lock, allocate dynamically, perform unbounded retry, or call back into the
  runtime; they complete within the driver's declared execution contract,
- cross-thread submission must be serialized by a platform/runtime adapter
  before entering the single-owner Core path.

This deliberately removes ambiguous lock/lifetime combinations from the
portable foundation.

### 4.1 Time-domain contract

Safety clocks expose a stable non-zero domain identifier, finite resolution,
a finite maximum read-latency bound and a declared maximum timestamp
uncertainty. A clock used for safety deadlines must advance across host
suspend/resume. Reads explicitly report healthy, unavailable or discontinuous
state and carry a conservative uncertainty bound that must not exceed the
declared maximum. Backward progression, a domain mismatch or an out-of-contract
uncertainty is a fail-closed condition for an armed watchdog.

The deadline watchdog treats a reading as an interval around its reported
value. Arm/kick use the conservative lower bound and expiry observation uses
the conservative upper bound, so clock uncertainty cannot silently extend a
safety deadline. A timeout must be strictly larger than twice the worse of the
clock resolution and declared maximum uncertainty; otherwise arming is rejected
with an explicit insufficient-precision status.

A received CAN timestamp is not an arbitrary device tick. Before entering Core,
the platform adapter maps native controller/VCI ticks into the configured Core
monotonic domain. Every successful RX carries the domain, mapped timestamp and
a conservative uncertainty bound. Runtime rejects a mismatched/unhealthy
timestamp, uncertainty above the driver's declared maximum, or backward
timestamp progression within one transport session.

The watchdog's `state()` is cached. `poll()` is the deadline-observation
operation and the owning scheduler must call it within the product's declared
safety service period.

### 4.2 CAN adapter session contract

The driver execution contract declares finite bounds for physical-lease
acquire/release, open, close, `try_send()`, `try_receive()` and RX timestamp
uncertainty.

- `try_send() == ok` accepts a frame exactly once into a bounded FIFO path.
- `would_block` and every other non-ok TX result accept nothing.
- fatal driver status quarantines/discards pending TX so it cannot leak into a
  later session.
- `close()` cancels/discards pending TX and clears RX/TX session queues before
  returning; old frames cannot appear after restart.
- RX loss is observable through an explicit dropped-frame count.
- `try_receive()` does not return an own-message echo of a frame accepted by
  the same driver instance. Analyzer/trace loopback may exist outside the
  authoritative protocol ingress, but protocol state machines receive only
  externally observed bus traffic.
- normal `stop()` closes the current adapter session but retains the
  physical-channel lease, allowing deterministic restart by the same runtime.
- driver-to-runtime reentrancy is forbidden; a reentrant stop observed while an
  external driver/arbiter call is active is treated as a contract violation and
  faults the runtime. Reentrancy during close retains the lease until explicit
  recovery.

## 5. Memory model

For foundational and time-critical paths:

- fixed-capacity arrays,
- bounded payloads,
- explicit capacity-exhausted status,
- no hidden dynamic growth,
- no exception/RTTI-based control paths in the deterministic foundation,
- no Core-owned mutex/atomic/thread-local synchronization; serialization belongs
  to the owning executor,
- direct source use of compiler/runtime hooks such as `memcpy`, `memset`
  or `__stack_chk_fail` is forbidden. A compiler may still lower aggregate
  copies/zeroing to the exact memory primitives or inject `__stack_chk_fail`
  for stack-protector hardening; CI treats only these exact compiler-generated
  symbols as acceptable and rejects every other external unresolved runtime
  symbol.

Dynamic allocation may exist in non-time-critical adapters/services later, but
it must not leak into deterministic Core contracts.

C++ object layout is not a persistence, network, VCI or IPC format. Padding,
`bool`, virtual ABI, `size_t` and compiler ABI may differ across targets.
Every external boundary uses an explicit versioned byte encoding instead of raw
serialization of Core structs.

## 6. Lifecycle model

Minimum phases:

```text
configuring -> ready -> running -> stopped
stopped -> running (validated restart)
ready/stopped/running -> faulted (fatal driver status)
faulted -> stopped (explicit recover)
```

Rules:

- subscriptions/registrations are accepted only in `configuring`,
- `freeze()` moves configuration to `ready`,
- runtime ownership starts from `ready` or `stopped` after successful open,
- a fault cannot silently return to `running`,
- `CanBusRuntime` cannot be copied or moved,
- successful start retains validated channel configuration, capabilities,
  physical-channel identity and driver execution/timestamp contract; Core
  validates TX/RX frames, rejects FD on classic channels and TX in listen-only,
- bus_off, io_error and not_open from open/TX/RX latch `faulted`; send/poll/start
  and stop perform no driver operations in that state,
- a successfully opened runtime retains the exclusive driver lease across
  faulted state even if the physical adapter is already closed; failed open
  releases the provisional lease immediately,
- destruction is a terminal cleanup exception: it invokes idempotent
  driver `close()` only when this runtime successfully opened/acquired it,
  including while faulted; failed open never transfers ownership,
- explicit `recover()` idempotently closes the driver, releases its exclusive
  lease and moves faulted to stopped; a separate validated start is required to
  run again. Ordinary stop cannot clear a fault and never releases the
  physical-channel lease, including from the stopped state,
- serialized subscriber callbacks reject nested send/poll/start/recover with
  busy; stop is deferred until the current frame reaches all matching
  subscribers, then closes the driver before any further receive. Callbacks
  cannot destroy the runtime or registered objects,
- external driver/arbiter calls are also protected against lifecycle reentrancy;
  a stop request originating during such a call faults with contract_violation,
- invalid frame payload/identifier RX is consumed but not delivered; invalid,
  mismatched or over-uncertain RX timestamp faults the runtime because timing
  provenance is part of the transport contract,
- close/recover creates a new transport session: pending TX and queued RX from
  the previous session do not survive restart,
- valid watchdog arm explicitly rearms from disarmed, armed or expired state;
  zero/negative or overflowing arm changes neither state nor deadline,
- watchdog arm rejects an unrepresentable deadline without changing state;
  kick with an unrepresentable deadline expires fail-closed,
- unhealthy clock reads, backward time, clock-domain mismatch or a clock that
  cannot guarantee suspend-continuous progression fail closed for an armed
  watchdog.

## 7. Heavy-duty protocol priority

Implementation priority after foundation:

1. CAN/CAN-FD shared-bus runtime — **DONE / foundation gate PASS**,
2. J1939 identifier + address/PGN model revalidation — **DONE / technical gate PASS**,
3. J1939 Address Claiming + opt-in Commanded Address — **subset DONE / technical gate PASS**,
4. J1939 Classical TP — **DONE / technical gate PASS**; ETP moves to the
   ISO 11783/ISOBUS extended-transport layer,
5. ISO 11783 / ISOBUS — **ETP DONE / technical gate PASS; network-management Control Function registry DONE / engineering gate PASS; Working Set Master/Member codec DONE / engineering gate PASS**; remaining application profiles stay module-gated,
6. J1939 diagnostics — **DM1/DM2/DM4/DM5/DM6/DM12 read-only subset DONE / technical gate PASS**,
7. J1939-22 CAN FD — **FBFF/FEFF no-assurance C-PG + no-assurance FD.TP DONE / engineering gate PASS**; assurance profiles remain gated,
8. ISO-TP revalidation on shared-bus ports — **DONE / engineering gate PASS; normative clause audit open**,
9. UDS transport-neutral foundation over ISO-TP adapter — **DONE / engineering gate PASS; normative clause audit open**,
10. DoIP,
11. ISO 11992 / WWH-OBD profiles where applicable.

This order reflects the actual TRUCK/AGRI/OHV product scope.

## 8. Core V2 acceptance policy

A Core V2 foundation gate requires at minimum:

- strict warning-as-error build,
- Debug and Release,
- independent GCC and Clang Linux validation (multi-compiler evidence),
- Windows MSVC Core-only Debug/Release validation on x64 and Win32,
- Generic non-Linux CMake-system build/test evidence,
- ASAN/UBSAN where supported,
- core-only build graph,
- portability and architecture gates,
- no external runtime/library symbols in the Core archive beyond intra-Core
  references on supported inspection toolchains,
- no dynamic static initialization sections in deterministic Core objects,
- no forbidden dynamic/thread/synchronization/platform/exception/RTTI APIs in
  deterministic foundation,
- negative/boundary/reentrancy/clock-domain/physical-lease tests,
- independent review,
- standards traceability status recorded honestly,
- machine-readable foundation conformance manifest validates requirement-to-code,
  requirement-to-test and responsibility-boundary evidence for TRUCK/AGRI/OHV.

The current Core V2 foundation has a scoped conformance PASS. No later protocol
module inherits that PASS: each protocol receives a standards PASS only after
its own declared normative scope has requirement-to-code, requirement-to-test
and interoperability evidence.

Driver `open()` is transactional: every non-ok return leaves the driver closed
and releases partial resources within the adapter. There is no externally visible
`stopping` state. A stop requested during dispatch is deferred until all matching
sinks receive the current frame; restart from `stopped` is supported.
