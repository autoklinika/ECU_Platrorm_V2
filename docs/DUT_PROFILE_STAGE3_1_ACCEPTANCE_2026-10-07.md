# ECU Platform V2 — DUT Profile Stage 3.1 acceptance

Date: 2026-10-07
Branch: `dut-profile/runtime-contract`
Code commit: `2d92984931126c41f50f6c91be6df1bdab9a0b3f`
Status: **ENGINEERING PASS**

## Scope accepted

Stage 3.1 establishes the DUT-neutral runtime profile contract above the
accepted Stage 3.0 definition/resolver layer.

Accepted elements:

- `IDutProfileProgram` as the profile-owned lifecycle semantics boundary,
- `DutProfileSessionEndpoint` adapter to Bench
  `IDutSessionEndpoint`,
- exact schema/profile-revision/profile-id identity matching before execution,
- immutable resolved-plan snapshot owned by the endpoint,
- frozen bounded execution contract captured at endpoint construction,
- fail-closed lifecycle:
  prepare -> activate -> service -> safe-stop -> stop,
- cleanup remains possible after profile-program faults,
- deterministic lifecycle counters/snapshot,
- resolved RX expectations carried from the validated profile into
  `ResolvedDutSessionPlan`,
- explicit constructor/composition injection for protocol runtimes and cyclic
  actuator runtime rather than a dynamic service locator.

## Architectural boundaries confirmed

Stage 3.1 does not introduce:

- direct physical CAN-driver access,
- platform-specific dependencies,
- hidden threads or sleeps,
- unbounded runtime containers,
- heap ownership,
- dynamic plugin discovery,
- concrete OEM/DUT wire semantics,
- changes to Core V2,
- changes to Bench Runtime Stage 2,
- selection of physical power/ignition/wake hardware.

## Local validation

`scripts/validate_dut_profile_stage3.sh` completed successfully on the final
code state.

Covered:

- GCC Debug,
- GCC Release,
- Generic non-Linux CMake target,
- ASAN/UBSAN,
- DUT Profile foundation tests,
- DUT Profile runtime contract tests,
- frozen Bench Runtime Stage 2 regression,
- frozen Core V2 regression.

Final local marker:

`DUT_PROFILE_STAGE3_LOCAL_GATE=PASS`

## GitHub CI evidence

Workflow run `37645804466` (run #54):

- result: **18/18 PASS**,
- DUT Profile Linux GCC: PASS,
- DUT Profile Linux Clang: PASS,
- DUT Profile Windows MSVC x64 Debug/Release: PASS,
- DUT Profile Windows MSVC Win32 Debug/Release: PASS,
- existing Core V2 and Bench Runtime jobs: PASS.

## Decision

Stage 3.1 is accepted as the common DUT Profile runtime contract.

The next generic layer is Stage 3.2: minimal fixed-capacity profile
registration/selection. It must remain static, deterministic and free of a
dynamic plugin framework.

DUT_PROFILE_STAGE3_1_ENGINEERING_PASS=YES
DUT_PROFILE_STAGE3_1_CORE_REVISION=NO
DUT_PROFILE_STAGE3_1_BENCH_REVISION=NO
DUT_PROFILE_STAGE3_1_PHYSICAL_HARDWARE_SELECTED=NO
DUT_PROFILE_STAGE3_1_CI=37645804466_18_OF_18_PASS
