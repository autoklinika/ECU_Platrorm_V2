# ECU Platform V2 — Laboratory DUT Architecture

Date: 2026-10-07
Status: **ARCHITECTURE BASELINE / REQUIRED BEFORE CORE FREEZE**

## 1. Product role

ECU Platform V2 is a laboratory platform for automotive electronics repair and engineering.
It is not defined as a diagnostic tester with auxiliary features.

The primary object connected to the bench is a Device Under Test (DUT). A DUT may be:

- an ECU or other complex electronic control module,
- a smart actuator such as EGR, VGT, throttle or valve actuator,
- a sensor or sensor module,
- a gateway or network node,
- another automotive electronic device that communicates through a supported physical/link interface.

ECU is therefore one DUT class, not the root abstraction of the product.

## 2. Core invariant

Generic Core must not assume that a connected DUT:

- supports diagnostics,
- supports UDS or J1939 diagnostics,
- implements J1939 Address Claim,
- is an ECU,
- participates in a complete vehicle network,
- accepts request/response traffic,
- uses one-shot CAN messages rather than cyclic control.

Raw CAN/CAN-FD traffic is a first-class Core use case, not a fallback mode.

Protocol stacks such as J1939, ISO-TP, UDS and ISOBUS are optional consumers above the shared bus runtime.
A DUT profile may use one of them, several of them, or none of them.

## 3. Why actuator-class DUTs are different

An actuator such as an EGR or VGT may use a proprietary cyclic CAN contract rather than a diagnostic protocol.
A typical laboratory control path can require:

- one or more cyclic command frames,
- cyclic feedback/status frames,
- strict transmission cadence,
- rolling counters or alive counters,
- checksum/CRC or OEM E2E fields,
- startup/enable handshakes,
- target ramping or slew limits,
- watchdog/command timeout behavior,
- deterministic safe-stop frames or command neutralization,
- correlation of command, measured position/current and error state.

These semantics belong to a deterministic actuation/runtime layer and a DUT-specific profile.
They must not be implemented in WebGUI, HTTP handlers or generic diagnostic services.

## 4. Target layer model

### A. Core V2

Core supplies platform-neutral primitives and deterministic execution contracts:

- CAN/CAN-FD ownership, routing and TX arbitration,
- monotonic time and deadlines,
- bounded queues and fixed-capacity state,
- raw frame validation and scheduling primitives,
- deterministic actuator execution primitives,
- interlock and safe-stop contracts,
- optional J1939 / ISO-TP / UDS / ISOBUS / future DoIP protocol layers,
- no DUT-specific identifiers, checksums, scaling or control algorithms.

### B. Bench Runtime / Bench Session

This layer is above generic Core and represents one laboratory connection/session.
It owns laboratory orchestration, not protocol definitions.

Responsibilities include:

- selected DUT and profile,
- physical bus/channel ownership requests,
- power/ignition/wake state requested from hardware services,
- start/stop lifecycle of required cyclic traffic,
- optional emulated partner/network context for a DUT that expects other nodes,
- safety/interlock state,
- session-level measurements and faults,
- deterministic transition to safe state on stop, timeout or transport failure.

A bench session must work for one DUT without constructing a synthetic full vehicle unless the selected profile explicitly requires it.

### C. DUT Profile

A profile describes what the connected hardware actually needs.

Common profile contract must support at least:

- DUT class,
- supported/required physical links,
- required communication mode(s),
- startup/shutdown sequence,
- receive filters,
- cyclic TX programs,
- raw frame encode/decode rules,
- counters/checksums/E2E functions,
- scaling and engineering units,
- safety limits and allowed commands,
- optional diagnostic transport/services,
- optional emulated partner frames.

Profiles are product/domain modules outside generic Core.

Examples:

- ECU profile: J1939 + UDS/ISO-TP + selected periodic environment PGNs,
- MAN Sonceboz EGR profile: proprietary cyclic CAN command/feedback behavior,
- VGT actuator profile: cyclic target/feedback/watchdog behavior,
- sensor module profile: passive/active acquisition with optional stimulus.

## 5. Required separation of concerns

### Protocol vs device behavior

`J1939`, `ISO-TP`, `UDS`, `ISOBUS` describe protocol behavior.
`MAN EGR`, `VGT X`, `Scania EMS S6` describe DUT behavior.

A DUT profile can bind protocol services and raw CAN programs, but generic protocol code must never depend on a concrete DUT.

### Raw CAN vs diagnostic transaction

Raw cyclic control is not forced through a diagnostic request/response API.
A diagnostic transaction is not forced through the actuator scheduler.
Both share the same authoritative `CanBusRuntime` and therefore cannot race ownership of a physical channel.

### GUI vs deterministic execution

WebGUI/API may request a high-level target or operation.
It must not generate time-critical CAN cadence.

For example:

`set EGR target 40%` -> Bench Runtime/Profile -> deterministic actuator runtime -> cyclic CAN frames

not:

`WebGUI timer` -> repeated HTTP/QML callbacks -> CAN send.

## 6. Safety model for active DUT control

Any active actuator/control profile must declare:

- safe/neutral state,
- command timeout,
- required feedback validity,
- upper/lower target limits,
- optional rate limits,
- required transport health,
- behavior on missing feedback,
- behavior on bus-off / I/O failure,
- stop behavior.

Generic Core supplies enforcement primitives; the concrete limits and frame encodings live in the profile.

A stopped/faulted session must not leave cyclic command traffic running.

## 7. Current legacy evidence

The retained project documentation records two relevant lessons from the previous ECU Platform:

1. MAN Sonceboz EGR protocol/control/autotest work existed and was validated on physical hardware; raw evidence was archived outside the current CM5 checkout.
2. Tight coupling of EGR control to the GUI was a fundamental legacy architecture problem. The later ActuatorEngine work demonstrated the need to separate time-critical control from presentation.

The legacy `IActuator` contract is reference evidence only. Core V2 must rebuild the contract and deterministic scheduler/interlock semantics rather than copy it unchanged.

## 8. Core freeze implications

Core V2 must not be frozen until the following laboratory foundations are closed:

1. DUT-neutral identity/class/capability contract.
2. Deterministic bounded cyclic CAN execution primitive for raw/proprietary devices.
3. Actuator safe-stop / timeout / interlock ownership contract.
4. Clear boundary between Core and Bench Session.
5. Regression evidence that raw CAN actuation can coexist with J1939/ISO-TP/UDS consumers on one shared bus runtime without direct driver access.
6. Multiplatform CI and sanitizer validation.

Full implementations of individual EGR/VGT/ECU profiles are not required to freeze generic Core, but at least one real actuator-class profile and one ECU-class profile must be used as architecture proof cases before production promotion.

## 9. Delivery sequence

### Stage 1 — Core finalization

- audit Core V2 against this DUT-neutral laboratory model,
- rebuild actuator/runtime primitives required by raw cyclic CAN DUTs,
- run final architecture/portability/conformance audit,
- freeze Core V2 only after the new gates pass.

### Stage 2 — Bench Runtime

- implement Bench Session lifecycle above Core,
- support one physical DUT as the default topology,
- support optional minimal environment emulation only when the profile requests it,
- keep power/ignition/wake hardware behind platform adapters.

### Stage 3 — DUT profiles

- common profile schema/contract,
- first actuator proof profile based on the retained MAN Sonceboz EGR knowledge/evidence,
- first ECU proof profile using the existing ECU/protocol work,
- add VGT and other devices without changing Core contracts.

Trace/replay capture infrastructure may be supplied by separate hardware and is not a prerequisite for this architecture.

CORE_V2_PRODUCT_ROLE=AUTOMOTIVE_ELECTRONICS_LAB_PLATFORM
CORE_V2_PRIMARY_ABSTRACTION=DUT
CORE_V2_RAW_CAN=FIRST_CLASS
CORE_V2_DIAGNOSTICS=OPTIONAL_CAPABILITY
CORE_V2_ACTUATOR_RUNTIME=ENGINEERING_PASS
CORE_V2_FREEZE_GATE=DUT_NEUTRAL_RUNTIME_AUDIT_PENDING
