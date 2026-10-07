# ECU Platform V2 — Cyclic CAN actuator runtime status

Date: 2026-10-07
Scope: TRUCK / AGRI / OHV laboratory actuator-class DUTs
Status: **ENGINEERING FOUNDATION PASS**

## Purpose

`CyclicCanActuatorRuntime` is the generic deterministic Core V2 execution
primitive for DUTs such as EGR, VGT, throttle/valve actuators and other
automotive devices controlled through proprietary cyclic CAN/CAN-FD traffic.

It is not a diagnostic service and has no dependency on UDS, J1939 or ISOBUS.

## Profile/Core boundary

The DUT-specific profile owns:

- CAN identifiers and frame format,
- payload encoding and scaling,
- rolling/alive counters,
- checksum, CRC or OEM E2E fields,
- target representation,
- safe/neutral payload semantics.

Generic Core owns:

- monotonic timing,
- cyclic cadence,
- maximum lateness,
- command freshness lease,
- optional feedback freshness lease,
- external interlock enforcement,
- bounded frame batches,
- TX through the authoritative `CanBusRuntime`,
- deterministic safe-stop transition,
- explicit fault latching and recovery,
- declared execution/WCET budgets.

## State and activation model

The runtime starts `interlocked`. Active control requires all of:

1. configured runtime,
2. healthy monotonic clock,
3. fresh high-level command lease,
4. explicitly opened external interlock,
5. successful first active-cycle render/TX.

The first active cycle is transmitted immediately. Later `service()` calls emit
at most one cycle and never perform catch-up bursts.

## Fail-closed conditions

Active control enters safe-stop/fault behavior on:

- command timeout,
- feedback timeout when feedback enforcement is configured,
- cyclic service later than the declared maximum lateness,
- monotonic clock failure/discontinuity,
- DUT profile render failure or invalid/over-bound batch,
- CAN TX failure/backpressure,
- explicit external interlock opening.

A late command or feedback refresh cannot revive an already-expired lease.
Fault recovery never resumes control automatically; the interlock must be closed
and the complete start/command/interlock/activate sequence repeated.

## Safe-stop model

A profile may define safe-stop as:

- one or more bounded neutral/disable CAN frames, or
- zero frames when the correct safe action is to cease cyclic command traffic.

If the profile cannot render its safe-stop sequence, or required safe-stop TX
cannot be accepted, the runtime reports `safe_stop_failed` explicitly.

## Deterministic execution contract

The DUT profile declares maximum active/safe render durations and frame counts.
Core combines those declarations with the active `CanBusRuntime` send bound and
the monotonic clock read bound.

Configuration fails if the normal active-cycle WCET cannot fit inside the
requested cyclic period.

The runtime exposes bounded maximum service, activation and safe-stop execution
durations to the owning scheduler.

## Important hard-real-time boundary

This is a deterministic single-executor Core state machine. It does not create
threads or timers and it never sleeps.

The host/platform scheduler MUST call `service()` within the published deadline.
If the host executes late, Core detects the miss and requests safe-stop on that
call. No software component running on the same stalled CPU can guarantee a
safe-stop while the host is completely hung or unpowered.

If a concrete actuator safety case requires fail-safe output independent of the
host CPU, the platform/hardware layer must provide an external watchdog, VCI/
MCU cyclic executor or equivalent hardware-enforced mechanism. That requirement
is deliberately outside the generic Core protocol/profile implementation.

## Executable evidence

Regression tests cover:

- fail-safe interlocked startup,
- explicit command + interlock activation,
- immediate first cyclic frame and exact next deadline,
- no early transmission,
- one-frame-per-service cadence,
- command timeout and late-refresh rejection,
- feedback timeout and refresh,
- cadence-miss detection,
- immediate interlock safe-stop,
- explicit stop,
- profile render fault,
- TX backpressure/transport fault,
- safe-stop render failure,
- safe-stop-by-silence profile,
- monotonic clock discontinuity,
- zero active batch rejection,
- frame-count bound enforcement,
- unsustainable WCET/period configuration rejection,
- explicit interlock requirement before fault reset.

The complete Core V2 validation passes Debug, Release, Generic non-Linux and
ASAN/UBSAN matrices with all current tests passing.

CORE_V2_CYCLIC_ACTUATOR_RUNTIME=PASS
CORE_V2_ACTUATOR_PROFILE_COUPLING=NO
CORE_V2_GUI_TIMING_OWNERSHIP=NO
CORE_V2_HARD_RT_PLATFORM_ENFORCEMENT=PLATFORM_GATED
