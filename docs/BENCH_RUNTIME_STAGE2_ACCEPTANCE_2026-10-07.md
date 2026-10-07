# ECU Platform V2 — Bench Runtime Stage 2 acceptance

Date: 2026-10-07
Branch: `bench/session-foundation`
Scope: DUT-neutral Bench Runtime above frozen Core V2
Status: **ENGINEERING FOUNDATION PASS**

## 1. Acceptance statement

Stage 2 establishes the DUT-neutral laboratory runtime required before concrete
DUT profiles are introduced.

The accepted foundation supports the default topology of one physical DUT and
keeps all concrete ECU/EGR/VGT/OEM behavior outside Bench Runtime.

No frozen `src/core_v2` source contract was modified to implement Stage 2.

## 2. Accepted components

### 2.1 Bench Session foundation

- deterministic session lifecycle,
- exclusive DUT plus declared additional resource leases,
- endpoint `prepare()` / `activate()` split,
- optional minimal environment session,
- hardware-neutral power / ignition / wake contract,
- generation-safe cancellation,
- ordered fail-closed safe shutdown,
- explicit recovery of incomplete cleanup,
- conservative execution budgets.

### 2.2 Observability

- versioned `BenchSessionSnapshot`,
- explicit state/status/reason/fault source,
- lifecycle revision,
- active and last-completed operation generations,
- bounded typed event publisher,
- Core `EventBus` adapter,
- fixed versioned event byte encoding,
- fail-visible observability degradation,
- bounded reentrancy behavior.

### 2.3 Resolved configuration handoff

- `BenchSessionConfig` is the resolved Stage-2 handoff,
- public side-effect-free `validate_configuration()` preflight,
- explicit failure reasons for DUT topology, resources, electrical requirements,
  wake/feedback requirements and environment requirements,
- resource contention remains transactional at `start()`,
- no concrete persistent DUT Profile schema is introduced in Stage 2.

### 2.4 Host service contract

- single-executor `BenchSessionHostRuntime`,
- no worker thread and no sleeps,
- injected monotonic clock,
- Core `DeadlineWatchdog` for host service deadline observation,
- fail-closed stop on observed deadline miss or clock fault,
- watchdog-arm rollback after a session has started,
- explicit host execution budgets,
- independent hardware/platform watchdog remains required when a completely
  stalled host is outside the DUT safety tolerance.

## 3. Boundary audit

Automated Bench architecture gate:

- direct physical CAN driver access: **NONE**,
- Linux/Windows/Qt dependency in Bench source: **NONE**,
- hidden threads/sleeps: **NONE**,
- unbounded runtime STL containers: **NONE**,
- heap ownership primitives in Bench foundation: **NONE**,
- concrete DUT coupling: **NONE**,
- dependency on frozen `ECU::core_v2`: **YES**,
- dependency on legacy `ECU::core`: **NO**.

The accepted Bench layer does not contain MAN EGR, Scania ECU, VGT or other
concrete DUT protocol semantics.

## 4. Local acceptance evidence

`bash scripts/validate_bench_runtime_stage2.sh`

Result:

- Bench architecture gate: PASS,
- GCC Debug: 4/4 Bench tests PASS,
- GCC Release: 4/4 Bench tests PASS,
- Generic non-Linux: 4/4 Bench tests PASS,
- ASAN/UBSAN: 4/4 Bench tests PASS,
- frozen Core V2 full regression: PASS,
- Core Debug: 20/20 PASS,
- Core Release: 20/20 PASS,
- Core Generic non-Linux: 20/20 PASS,
- Core ASAN/UBSAN: 20/20 PASS,
- Core isolated graph / external-symbol / dynamic-static-init gates: PASS.

Final local marker:

`BENCH_RUNTIME_STAGE2_LOCAL_GATE=PASS`

## 5. Cross-platform evidence before final acceptance commit

Accepted increments:

- Stage 2.1 session foundation: CI `37629709476` — 12/12 PASS,
- Stage 2.2 observability: CI `37632940664` — 12/12 PASS,
- Stage 2.3 resolved config preflight: CI `37633720909` — 12/12 PASS,
- Stage 2.4 host service deadline guard: CI `37634511394` — 12/12 PASS.

The final Stage 2 acceptance commit must also pass the same current
cross-platform CI matrix before the branch is treated as the Stage 2 baseline.

## 6. What Stage 2 does not claim

Stage 2 does not select production hardware for B+/KL30, IGN/KL15 or wake.

Stage 2 does not implement:

- a concrete ECU,
- MAN Sonceboz EGR frames,
- a VGT profile,
- OEM counters/checksums/E2E,
- a complete vehicle simulator,
- GUI/WebGUI,
- formal product functional-safety approval,
- hard-real-time certification.

A completely stalled host cannot be made safe by software running on that same
stalled CPU; the platform/hardware safety case remains separate.

## 7. Next layer

The next engineering layer is Stage 3 — common DUT Profile contract.

Stage 3 must resolve a concrete profile into the accepted Bench Runtime
boundaries rather than modifying Core for profile-specific convenience.

The first real proof cases remain:

1. actuator-class DUT: retained MAN Sonceboz EGR evidence,
2. ECU-class DUT using the already available Core protocol foundations.

Those proof cases start only after the common DUT Profile contract itself has a
DUT-neutral technical gate.

BENCH_RUNTIME_STAGE2_CORE_REVISION=NO
BENCH_RUNTIME_STAGE2_PRIMARY_TOPOLOGY=ONE_PHYSICAL_DUT
BENCH_RUNTIME_STAGE2_POWER_HARDWARE_SELECTED=NO
BENCH_RUNTIME_STAGE2_LOCAL_GATE=PASS
BENCH_RUNTIME_STAGE2_ENGINEERING_FOUNDATION=PASS_PENDING_FINAL_CI
