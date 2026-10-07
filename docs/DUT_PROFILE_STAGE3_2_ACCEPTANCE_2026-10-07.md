# ECU Platform V2 — DUT Profile Stage 3.2 acceptance

Date: 2026-10-07
Branch: `dut-profile/registry-selection`
Code commit: `950dad2a7bad669fc4eb7b1ef53d5a64e72c42eb`
Status: **ENGINEERING PASS**

## Scope accepted

Stage 3.2 adds the minimal deterministic registration/selection layer required
by the application boundary.

Accepted elements:

- `DutProfileRegistry` with fixed capacity of 32 profiles,
- profile definitions are validated before registration,
- definitions are stored by value and cannot be mutated through their source
  objects after registration,
- `profile_id` is unique within the registry,
- deterministic insertion-order enumeration,
- explicit configuration freeze,
- runtime selection only after freeze,
- stable selection by `profile_id`,
- explicit distinction between invalid id, unknown id and non-frozen registry,
- fail-closed capacity exhaustion.

## Architectural boundaries confirmed

Stage 3.2 does not introduce:

- filesystem discovery,
- dynamic library loading,
- a plugin manager,
- heap ownership,
- unbounded containers,
- platform-specific APIs,
- physical CAN-driver access,
- concrete ECU/EGR/VGT semantics,
- changes to Core V2,
- changes to Bench Runtime Stage 2.

## Local validation

`scripts/validate_dut_profile_stage3.sh` completed successfully on the final
Stage 3.2 code state.

DUT Profile tests:

- foundation: PASS,
- runtime: PASS,
- registry: PASS.

The full frozen Bench Stage 2 and Core V2 regression suite also passed,
including Generic non-Linux and sanitizer builds.

Final local marker:

`DUT_PROFILE_STAGE3_LOCAL_GATE=PASS`

## GitHub CI evidence

Workflow run `37646999518` (run #59):

- result: **18/18 PASS**,
- DUT Profile Linux GCC: PASS,
- DUT Profile Linux Clang: PASS,
- DUT Profile Windows MSVC x64 Debug/Release: PASS,
- DUT Profile Windows MSVC Win32 Debug/Release: PASS,
- existing Core V2 and Bench Runtime jobs: PASS.

## Decision

Stage 3.2 is accepted. The neutral DUT Profile infrastructure is now sufficient
to proceed to proof profiles without adding a dynamic plugin system.

Next: Stage 3.3 proof profiles. Concrete semantics must be derived from retained
evidence, not guessed. Any new device requirement is handled in DUT Profile /
Bench Runtime first; Core V2 changes only for a demonstrated architectural gap.

DUT_PROFILE_STAGE3_2_ENGINEERING_PASS=YES
DUT_PROFILE_STAGE3_2_CORE_REVISION=NO
DUT_PROFILE_STAGE3_2_BENCH_REVISION=NO
DUT_PROFILE_STAGE3_2_DYNAMIC_PLUGINS=NO
DUT_PROFILE_STAGE3_2_CI=37646999518_18_OF_18_PASS
