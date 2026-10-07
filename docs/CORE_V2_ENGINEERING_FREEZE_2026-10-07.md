# ECU Platform V2 — Core V2 engineering foundation freeze

Date: 2026-10-07
Branch baseline: `core-v2/runtime-foundation`
Source gate commit: `d96e4c18b5543eadd4c3b36394de98044624eb4f`
Source CI run: `37622968497` — 6/6 PASS
Status: **ENGINEERING FOUNDATION FREEZE PASS**

## 1. Meaning of this freeze

This freeze closes the generic Core V2 engineering foundation required for the
ECU Platform laboratory model. It does not merge to production `main` and does
not claim formal SAE/ISO conformance, product functional-safety approval or
hard-real-time certification.

From this point, Bench Session and DUT profiles should be built above these
contracts. New DUTs must not cause ad-hoc changes to generic Core.

## 2. Frozen architectural contracts

### Product/domain

- product scope remains TRUCK / AGRI / OHV,
- root laboratory abstraction is DUT, not ECU,
- ECU, actuator, sensor, gateway and generic automotive network node are valid DUT classes,
- raw CAN/CAN-FD is first-class and diagnostics are optional capabilities.

### Transport ownership

- `ICanDriver` is the physical adapter boundary,
- one authoritative `CanBusRuntime` owns a physical CAN channel,
- protocols and DUT profiles never consume the physical driver directly,
- RX is routed by frozen subscriptions,
- TX is centralized through the runtime,
- Classic CAN and CAN-FD share the same ownership model.

### Time and deterministic execution

- timing-sensitive Core paths use injected monotonic clock contracts,
- no Core worker threads, sleeps or GUI/event-loop timing ownership,
- bounded execution contracts are explicit where external callbacks/drivers are involved,
- cyclic actuator execution is single-executor, bounded and fail-closed.

### Runtime semantics

- runtime topology is configured and then frozen,
- resource ownership uses generation-safe leases,
- commands are typed and policy-gated,
- state snapshots are revisioned and caller-buffered,
- events have Core-owned sequence and monotonic timestamp,
- module and DUT registries are fixed-capacity,
- cancellation is generation-safe,
- reentrant command/event execution returns explicit `busy` rather than blocking.

### Laboratory actuation

- profile owns OEM/raw wire semantics: IDs, payload, counter, checksum/CRC/E2E, scaling and neutral frame,
- Core owns cadence, lateness, command/feedback freshness, interlock and safe-stop execution,
- late leases cannot revive an expired active operation,
- safe-stop failure is explicit,
- a completely stalled host requires an independent platform/hardware watchdog when the DUT safety case demands it.

### Protocol foundations already accepted at engineering level

- J1939 identifier/PGN/NAME and network-management foundation,
- J1939 Classical TP and selected read-only diagnostics,
- J1939-22 no-assurance CAN-FD foundation,
- ISO 11783 ETP, network management and read-only Working Set lifecycle foundation,
- ISO-TP shared-bus transport foundation,
- transport-neutral UDS foundation with ISO-TP end-to-end evidence.

Normative clause audits remain separate gates.

## 3. DUT-neutral proof

An executable proof runs J1939 NetworkManager, ISO-TP and a proprietary cyclic
raw-CAN actuator at the same time on one `CanBusRuntime`.

Evidence demonstrates:

- protocol and actuator TX all pass through centralized runtime TX,
- mixed standard/extended RX is delivered only to matching subscriptions,
- raw actuator operation does not require UDS/J1939,
- actuator safe-stop does not alter protocol state.

This is the key proof that an ECU-style DUT and an EGR/VGT-style DUT fit one
generic Core without changing transport ownership.

## 4. Freeze evidence

Local full gate on the source baseline:

- Debug: 20/20 PASS,
- Release: 20/20 PASS,
- Generic non-Linux CMake system: 20/20 PASS,
- ASAN/UBSAN: 20/20 PASS,
- architecture gate: PASS,
- portability gate: PASS,
- scoped standards/conformance gate: PASS,
- negative architecture/conformance gates: PASS,
- external runtime symbols: PASS,
- dynamic static initialization: PASS.

GitHub CI run `37622968497`:

- Core Linux x86_64: PASS,
- Core V2 Linux Clang: PASS,
- MSVC Win32 Debug: PASS,
- MSVC Win32 Release: PASS,
- MSVC x64 Debug: PASS,
- MSVC x64 Release: PASS.

Final static audit found:

- no Linux/Qt/SocketCAN dependency in `src/core_v2`,
- no `std::thread`, mutex, condition variable or sleep in Core V2,
- no dynamic STL containers or heap ownership in Core V2 critical foundation,
- no direct `ICanDriver`/`try_send`/`try_receive` access from protocol, actuation, runtime, domain or safety layers.

## 5. Explicitly not required for this foundation freeze

The following are deliberately separate, use-case or product gates and do not
justify reopening generic Core merely because they are not implemented yet:

- deeper ISOBUS VT/Task Controller/TIM/application behavior,
- ISO 11992/WWH-OBD expansion,
- DoIP transport implementation and concrete network adapters,
- additional state-changing UDS/J1939 diagnostics,
- J1939-22 assurance profiles,
- persistent storage implementation,
- product Root of Trust/licensing/update security,
- reusable simulated-CAN tooling,
- capture/replay acquisition supplied by separate hardware,
- GUI/API implementation.

## 6. Next layer after freeze

Development now moves above Core:

1. Bench Session for one physical DUT as the default topology.
2. Optional minimal environment emulation requested by the active DUT profile.
3. Common DUT Profile contract.
4. First actuator proof profile from retained MAN Sonceboz EGR knowledge.
5. First ECU proof profile from the existing ECU/protocol work.

## 7. Change-control rule

A new profile or Bench Session feature is not sufficient reason to modify frozen
Core contracts. A breaking Core change requires an explicit Core revision,
documented architectural reason and complete Core re-gate.

Production merge/release remains a separate decision owned by the user.

CORE_V2_ENGINEERING_FOUNDATION_FREEZE=PASS
CORE_V2_PRODUCT_ROLE=AUTOMOTIVE_ELECTRONICS_LAB_PLATFORM
CORE_V2_PRIMARY_ABSTRACTION=DUT
CORE_V2_SHARED_BUS_DUT_PROOF=PASS
CORE_V2_CROSS_PLATFORM_CI=PASS
CORE_V2_NORMATIVE_PROTOCOL_AUDITS=OPEN
CORE_V2_PRODUCTION_MERGE=NOT_AUTHORIZED
