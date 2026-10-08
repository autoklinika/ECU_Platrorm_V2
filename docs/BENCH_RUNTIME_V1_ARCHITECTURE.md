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

## 6.1 Observability contract

Bench observability is not allowed to become the owner of session execution.

`BenchSessionSnapshot` is a source-level typed snapshot with schema version,
lifecycle revision, state/status/reason, fault source, DUT profile ID, resource
count, active/last-completed operation generation, counters and degradation
flags.

Bench lifecycle events are published through `IBenchSessionEventPublisher`.
The Core integration is a thin `BenchSessionEventBusPublisher` adapter over the
already-frozen Core `EventBus`; Bench Session does not implement a second
generic event bus.

The Core event payload uses a fixed v1 byte encoding. C++ object layout is never
used as event/wire ABI. Reserved bytes and unknown enum values fail closed in
the decoder.

Event publication time is included in the Bench execution budgets. A failed
event publication increments an explicit failure counter and latches
`observability_degraded`, but it does not roll back a completed safe lifecycle
transition. Safety/control remains authoritative over telemetry delivery.

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

## 7.1 Resolved configuration handoff

Stage 2 deliberately does not define a persistent DUT Profile schema.

The future Stage 3 profile resolver produces a resolved `BenchSessionConfig`
for one session. The configuration contains only runtime requirements already
understood by Bench Runtime: selected DUT handle, resource keys,
power/ignition/wake request, optional feedback verification and optional minimal
environment mode.

`validate_configuration()` is a side-effect-free preflight. It returns a
specific `BenchSessionConfigValidationStatus` and does not acquire hardware or
resources. This lets a future application service/Profile resolver reject an
impossible plan before any physical DUT action occurs.

Exclusive resource availability is intentionally not claimed during preflight;
`ResourceManager` ownership is still acquired transactionally by `start()`,
so a configuration that was structurally valid can still return
`resource_unavailable` if another owner acquired the resource in the meantime.

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

**Engineering foundation PASS on this branch.**

### 2.2 — Bench observability

- stable versioned `BenchSessionSnapshot`,
- explicit lifecycle revision and reason/fault model,
- active and last-completed cancellation generation visibility,
- typed `IBenchSessionEventPublisher` boundary,
- bounded Core `EventBus` adapter,
- explicit versioned byte encoding for event payloads,
- transition/fault/operation events,
- observability degradation is fail-visible but does not corrupt safe lifecycle,
- reentrant lifecycle calls from event callbacks return bounded `busy`,
- no GUI dependency.

**Engineering PASS. Cross-platform CI run `37632940664`: 12/12 PASS.**

### 2.3 — Session configuration handoff

- `BenchSessionConfig` is the neutral resolved handoff from future DUT Profile
  resolution into Bench Runtime,
- public `validate_configuration()` performs deterministic preflight before
  session ownership starts,
- explicit validation statuses cover DUT topology/handle, resource list,
  electrical boundary/capabilities, wake pulse, feedback and environment,
- DUT capability requirements for environment/power/wake are enforced before
  `start()`,
- resource availability/contention remains a start-time ownership check,
- concrete OEM/profile schema remains Stage 3.

**Engineering PASS. Cross-platform CI run `37633720909`: 12/12 PASS.**

### 2.4 — Host/platform service contract

- `BenchSessionHostRuntime` is a single-executor host orchestration layer,
- it creates no worker thread and performs no sleep,
- host cadence is expressed as an explicit `service_timeout`,
- the accepted Core `DeadlineWatchdog` guards the interval between successful
  service cycles,
- timeout must contain the complete conservative Bench service execution path,
- deadline miss or clock fault causes fail-closed `BenchSession::stop()`,
- watchdog arm failure after DUT start immediately rolls the session back to a
  safe stopped/ready state,
- cancellation completion automatically disarms the host watchdog,
- a completely stalled host still requires an independent platform/hardware
  watchdog when the DUT safety case requires it,
- still no concrete production power hardware.

**Engineering PASS. Cross-platform CI run 37634511394: 12/12 PASS.**

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

## 10.1 Host service ownership

`BenchSessionHostRuntime` defines the host-facing service contract without
becoming an OS scheduler. The application/platform executor is responsible for
calling `service()` before the configured timeout; Bench Runtime only checks the
contract and fails closed when a late call is finally observed.

The host runtime arms its Core deadline watchdog only after `BenchSession`
reaches `running`. A normal service cycle performs watchdog poll -> Bench
service -> watchdog kick. If the deadline has already expired, if the monotonic
clock becomes unhealthy, or if a kick cannot be completed safely, the host
runtime invokes the Bench safe-stop path and disarms itself.

The host timeout is rejected if it is too short for clock precision or for the
conservative maximum service execution path. Exposed host execution budgets
include the underlying Bench budgets plus clock reads and worst-case safe-stop
cleanup.

This design detects scheduling failure but cannot execute while the host CPU is
completely stalled. Independent hardware/platform protection remains required
for DUTs whose safety case cannot tolerate that failure mode.

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
BENCH_RUNTIME_STAGE2_FOUNDATION=ENGINEERING_PASS
