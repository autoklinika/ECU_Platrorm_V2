# ECU Platform V2 — Stage E portable Core bootstrap

## Status

Status: **VERIFIED / PASS**\n\nScope: **portable Core build skeleton + architectural portability gate**.

Stage E intentionally does **not** freeze the still-open platform contracts or API technology choices.

## Why this stage exists

Stages A-D established Prototype A:

- clean Raspberry Pi OS Lite baseline,
- CAN/CAN-FD hardware availability,
- remote maintenance,
- local WebGUI kiosk runtime.

The next safe step is to create the first compilable product-code boundary while protecting the project's highest-priority invariant:

> ECU Platform V2 Core must remain independent of the current OS and hardware.

## Stage E scope

Stage E adds:

1. root CMake build,
2. standalone `ecu_core` C++ library,
3. minimal smoke test,
4. explicit source-level portability gate,
5. a portability-gate self-test,\n6. one validation script that configures, builds and tests the Core.

No new operating-system package is required on Prototype A because Stage A already installed CMake, Ninja and G++.

## Explicit non-decisions

Stage E does **not** decide or implement:

- the final C++ language level for the commercial product,
- the formal `ICanInterface` / CAN transport contract,
- clock/scheduler interface,
- storage interface,
- networking/DoIP platform interface,
- hardware-I/O interface,
- device identity/security interface,
- Core process/service topology,
- extension/plugin model,
- HTTP/WebSocket framework,
- WebGUI frontend framework,
- database,
- Docker/Podman,
- Linux SocketCAN adapter.

Those remain separate project decisions.

The CMake target currently requests C++17 only as a conservative bootstrap minimum so the first Core target builds reproducibly. No product API is allowed to depend on that choice yet.

## Source layout introduced

```text
CMakeLists.txt
src/
  core/
    CMakeLists.txt
    include/ecu/core/build_info.hpp
    src/build_info.cpp
tests/
  CMakeLists.txt
  core_smoke.cpp
scripts/
  check_core_portability.sh
  validate_stage_e_core_bootstrap.sh
```

## Portability gate

`scripts/check_core_portability.sh` scans Core source files and fails on direct use of known Prototype-A/Linux dependencies including:

- Linux kernel headers,
- system socket/ioctl/network headers,
- systemd/libudev headers,
- SocketCAN references,
- Raspberry Pi-specific libraries,
- GPIO/spidev implementation references,
- hard-coded `/dev`, `/sys`, `/proc` paths,
- shell control such as `systemctl` or `ip link`.

The gate is deliberately scoped to Core source. Future Linux-specific adapters are allowed to use Linux APIs in their own adapter directories.

## Validation

Run:

```bash
cd ~/ECU_Platrorm_V2
./scripts/validate_stage_e_core_bootstrap.sh
```

Expected final marker:

```text
STAGE_E_CORE_BOOTSTRAP=PASS
```

The validation performs:

- repository status display,
- portability scan,
- clean CMake/Ninja configuration,
- warning-as-error build,
- CTest,
- direct smoke execution.

## Acceptance criteria

Stage E is VERIFIED only when:

1. `ecu_core` builds from a clean build directory,
2. warnings are treated as errors,
3. CTest passes,
4. direct Core smoke returns `ECU_CORE_SMOKE=PASS`,
5. portability gate returns `CORE_PORTABILITY_GATE=PASS`,
6. final validation returns `STAGE_E_CORE_BOOTSTRAP=PASS`,
7. no Linux/Raspberry Pi implementation has entered Core,
8. production `main` remains unchanged.

## Next architectural gate

After Stage E, the next design discussion should define the **minimum formal platform/transport contracts** before adding SocketCAN or real ECU protocol logic. That is a project-level decision and is not silently made by this stage.


## Verification evidence — 2026-10-06

Result: **VERIFIED / PASS**

Validated directly on Prototype A:

- compiler: GNU C++ `14.2.0`
- generator: Ninja
- Debug configure/build: PASS
- Release configure/build: PASS
- warnings-as-errors build: PASS
- CTest: `1/1` PASS in Debug
- CTest: `1/1` PASS in Release
- direct smoke: `ECU_CORE_SMOKE=PASS`
- real Core portability scan: `CORE_PORTABILITY_GATE=PASS`
- portability guard self-test:
  - portable fixture: accepted
  - fixture containing `#include <linux/can.h>`: rejected
  - final marker: `CORE_PORTABILITY_SELFTEST=PASS`
- final validator: `STAGE_E_CORE_BOOTSTRAP=PASS`
- repository working tree remained clean after validation
- no new OS packages were installed
- production `main` remained unchanged

The first compiled Core target therefore exists without importing Linux, Raspberry Pi, SocketCAN, systemd, GPIO, SPI or filesystem-device implementation details.

### Stage E closure

**Stage E is complete.**

The next stage must not add a real Linux/SocketCAN implementation until the minimum platform/transport contracts have been explicitly designed and accepted.
