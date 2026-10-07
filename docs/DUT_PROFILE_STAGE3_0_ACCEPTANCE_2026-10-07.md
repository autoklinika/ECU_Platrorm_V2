# ECU Platform V2 — DUT Profile Stage 3.0 acceptance

Date: 2026-10-07
Branch: `dut-profile/foundation`
Status: **ENGINEERING PASS**

## Scope accepted

Stage 3.0 establishes the DUT-neutral profile definition and resolver above the frozen Bench Runtime Stage 2.

Accepted elements:

- fixed-capacity `DutProfileDefinition`,
- strict schema/profile validation,
- logical resource roles,
- CAN link requirements without physical driver objects,
- expected CAN RX filters,
- protocol requirement declarations for J1939 / ISO-TP / UDS / ISOBUS,
- electrical power / ignition / wake requirements,
- minimal environment requirement,
- cyclic-control timing metadata,
- `DutProfileBinding` from logical roles to physical bench `ResourceKey` values,
- resolution against a frozen Core `DutRegistry`,
- generation of `ResolvedDutSessionPlan` and `BenchSessionConfig`,
- synthetic ECU-style and actuator-style proof coverage.

## Architectural boundaries confirmed

Stage 3.0 does not introduce:

- concrete OEM/DUT semantics,
- direct physical CAN-driver access,
- Linux/Windows/Qt dependencies,
- hidden threads or sleeps,
- unbounded runtime containers,
- heap ownership,
- a selected power-control implementation,
- changes to Core V2,
- changes to Bench Runtime Stage 2.

## Local validation

`scripts/validate_dut_profile_stage3.sh` completed successfully.

Covered:

- GCC Debug,
- GCC Release,
- Generic non-Linux CMake target,
- ASAN/UBSAN,
- frozen Bench Runtime Stage 2 regression,
- frozen Core V2 regression.

Final local marker:

`DUT_PROFILE_STAGE3_LOCAL_GATE=PASS`

## GitHub CI evidence

Final Stage 3.0 CI run:

- workflow run: `37643059182`,
- result: **18/18 PASS**,
- DUT Profile Linux GCC: PASS,
- DUT Profile Linux Clang: PASS,
- DUT Profile Windows MSVC x64 Debug/Release: PASS,
- DUT Profile Windows MSVC Win32 Debug/Release: PASS,
- existing Core V2 and Bench Runtime jobs: PASS.

## Decision

Stage 3.0 is accepted as the neutral DUT Profile foundation.

Next stage is Stage 3.1: runtime profile contract bound to `IDutSessionEndpoint`, while keeping concrete ECU/EGR/VGT behavior out of the common layer.

DUT_PROFILE_STAGE3_0_ENGINEERING_PASS=YES
DUT_PROFILE_STAGE3_0_CORE_REVISION=NO
DUT_PROFILE_STAGE3_0_BENCH_REVISION=NO
DUT_PROFILE_STAGE3_0_PHYSICAL_HARDWARE_SELECTED=NO
DUT_PROFILE_STAGE3_0_CI=37643059182_18_OF_18_PASS
