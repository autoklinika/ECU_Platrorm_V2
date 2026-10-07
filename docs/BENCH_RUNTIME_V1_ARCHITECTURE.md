# ECU Platform V2 — Bench Runtime / Bench Session v1

Date: 2026-10-07  
Branch: `bench/session-foundation`  
Status: **STAGE 2 FOUNDATION IN DEVELOPMENT**

## 1. Entry gate from frozen Core V2

Stage 2 can start without changing frozen Core V2.

The required generic primitives already exist in Core V2:

- DUT-neutral identity and capabilities,
- generation-safe resource ownership,
- authoritative CAN runtime and protocol layers,
- deterministic cyclic actuator runtime,
- monotonic timing contracts,
- generation-safe cancellation,
- bounded lifecycle/runtime primitives.

Bench Runtime therefore consumes Core contracts but does not add DUT-specific
behavior to Core.

**Core revision required for Stage 2 entry: NO.**

Any future request to change a frozen Core contract still requires a separate
architectural justification and complete Core re-gate.

## 2. Responsibility boundary

### Core V2

Core owns only generic, platform-neutral primitives:

- physical-link ownership contracts,
- CAN/CAN-FD routing and centralized TX,
- protocol foundations,
- runtime/resource/cancellation primitives,
- deterministic actuation primitives,
- generic safety/deadline mechanisms.

Bench Runtime may depend on Core V2. Core V2 must never depend on Bench Runtime.

### Bench Runtime / Bench Session

Bench Runtime owns one laboratory session with one physical DUT as the default
topology.

It owns orchestration:

- selected registered DUT,
- acquisition and release of session resources,
- preparation and activation of the DUT runtime endpoint,
- optional minimal environment session,
- hardware-neutral power/ignition/wake requests,
- start/service/stop ordering,
- cancellation,
- fault propagation,
- fail-closed safe shutdown,
- explicit recovery,
- conservative execution-budget aggregation.

It does not define OEM CAN frames, diagnostic identifiers, actuator scaling,
checksums, counters or concrete power hardware.

### DUT Profile layer

The future DUT Profile layer will describe a concrete DUT and bind its behavior
to the Bench Runtime boundary.

A profile may provide or compose the `IDutSessionEndpoint` implementation and
request:

- required resources,
- protocol modules,
- cyclic actuation,
- environment emulation,
- power/ignition/wake behavior,
- safety constraints.

The profile must not access a physical CAN driver directly.

### Platform adapters

Platform adapters implement physical services behind contracts.

For Stage 2 the electrical boundary is `IBenchElectricalControl`. It exposes
logical capabilities only:

- power,
- ignition,
- wake level,
- wake pulse,
- optional state feedback,
- optional voltage feedback,
- optional current feedback,
- safe-off.

No Raspberry Pi GPIO, relay board, high-side switch or CAN I/O product is
selected by this contract.

A wake-pulse command arms/schedules the pulse in the adapter and must return
within its declared execution bound. It must not sleep for the requested pulse
width.

### Future GUI/API

GUI/API is a client of Bench Runtime.

It may request high-level operations such as:

- configure/select session,
- start,
- stop,
- cancel,
- recover,
- profile commands.

GUI/API never owns:

- cyclic CAN cadence,
- power sequencing timing,
- protocol service timing,
- safe-stop execution,
- resource cleanup.

## 3. Bench Session v1 state model

Public stable states in the first foundation are:

```text
unconfigured
    |
    v
ready
    |
    v
starting --> faulted
    |
    v
running
    |
    +--> stopping --> ready
    |        |
    |        +--> faulted
    |
    +--> faulted
             |
             v
         recovering
             |
             +--> ready
             +--> faulted
```

Cancellation is an operation outcome, not a persistent lifecycle state.

A successful cancellation performs the same safe shutdown as normal stop,
completes the current generation-scoped cancellation token and returns the
session to `ready`.

A stale cancellation token cannot cancel a later session generation.

## 4. Start sequencing

The v1 start sequence is deliberately generic:

1. begin a new cancellation generation,
2. acquire the DUT resource,
3. acquire all declared additional resources,
4. request electrical safe-off when electrical control is used,
5. prepare the DUT endpoint,
6. start minimal environment emulation when requested,
7. apply the requested run electrical state,
8. arm a wake pulse when requested,
9. optionally verify electrical state feedback,
10. activate the DUT endpoint,
11. enter `running`.

`prepare()` is intentionally separate from `activate()`.

This lets the endpoint open transports/listeners before the DUT is powered or
woken while preventing active DUT actuation from starting during preparation.

## 5. Safe shutdown ordering

For stop, cancellation and fault cleanup the v1 order is:

1. DUT endpoint safe-stop,
2. electrical safe-off,
3. environment stop,
4. DUT endpoint stop/transport teardown,
5. release all session resource leases.

Cleanup continues after an individual failure so that later safety actions are
still attempted.

A cleanup failure is never silently converted to `ready`.

If a component cannot be torn down, `cleanup_required` remains latched and an
explicit `recover()` retries the unfinished cleanup.

Even when a later teardown action makes the bench physically safe, any
safe-shutdown contract failure remains visible as a fault until explicit
recovery.

## 6. Execution bounds

Every externally supplied session component declares finite execution bounds.

Bench Session aggregates conservative worst-case budgets for:

- start including worst-case cleanup after a late start failure,
- one service call including worst-case cleanup after a runtime fault,
- normal safe stop.

The current foundation does not claim hard real-time execution. Platform and
host scheduling guarantees remain separate acceptance gates.

## 7. Resource model

Bench Session always acquires an exclusive logical DUT resource plus all
additional resources requested for the session.

Examples of future additional resources include:

- CAN channel,
- diagnostic channel,
- actuator engine,
- power domain,
- hardware I/O.

The list is fixed-capacity and duplicate resource requests are rejected.

A profile cannot smuggle a second `device_under_test` resource through the
additional-resource list.

## 8. Minimal environment rule

`EnvironmentMode::minimal_profile_environment` exists only for DUTs that need
a small amount of surrounding network behavior to operate on the bench.

It is not a vehicle simulator.

No environment session is started unless requested by the session/profile.

## 9. Power / ignition / wake rule

Stage 2 defines only the hardware-neutral control boundary.

Concrete hardware selection is deferred until the first real platform adapter.

The boundary can represent:

- power on/off through desired state,
- ignition on/off through desired state,
- wake level,
- wake pulse,
- state/voltage/current feedback capability,
- safe-off.

Profile-specific delays and multi-step OEM startup procedures are not encoded
as generic Core behavior.

## 10. Stage 2 implementation plan

### 2.1 — Session foundation

- separate `ECU::bench` library,
- lifecycle and deterministic ordering,
- resource ownership,
- DUT endpoint prepare/activate split,
- minimal environment boundary,
- electrical adapter boundary,
- cancellation,
- safe shutdown and recovery,
- execution budgets,
- unit tests without hardware.

**Implementation started in this branch.**

### 2.2 — Bench observability

- stable session snapshot,
- reason/status model suitable for API use,
- bounded event publication for transitions/faults,
- no GUI dependency.

### 2.3 — Session configuration handoff

- define the neutral handoff from future DUT Profile resolution into
  `BenchSessionConfig`,
- validate required resources/capabilities before start,
- keep concrete profile schema in Stage 3.

### 2.4 — Host/platform service contract

- define how the host calls `service()`,
- document required service cadence and failure handling,
- provide test/fake adapters,
- still no concrete production power hardware.

### 2.5 — Stage 2 acceptance gate

- GCC Debug/Release,
- Clang,
- Generic non-Linux compile,
- ASAN/UBSAN where supported,
- Windows MSVC x64/Win32 Debug/Release,
- architecture audit confirming no direct physical CAN-driver ownership,
- Core V2 regression gate unchanged.

After this gate, Stage 3 may introduce the common DUT Profile contract and the
first real proof profiles.

## 11. Explicit non-goals for Stage 2 foundation

Do not add here:

- MAN EGR frame definitions,
- a concrete ECU implementation,
- VGT-specific behavior,
- OEM checksums/counters,
- GUI or WebGUI,
- complete vehicle simulation,
- Linux-specific power control,
- Raspberry Pi GPIO logic,
- DoIP or deeper J1939/ISOBUS work without a DUT use case.

BENCH_RUNTIME_CORE_REVISION_REQUIRED=NO  
BENCH_RUNTIME_PRIMARY_TOPOLOGY=ONE_PHYSICAL_DUT  
BENCH_RUNTIME_GUI_OWNS_TIMING=NO  
BENCH_RUNTIME_POWER_HARDWARE_SELECTED=NO  
BENCH_RUNTIME_STAGE2_FOUNDATION=IN_DEVELOPMENT
