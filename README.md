# ECU Platform V2

Laboratory platform for repair and testing of automotive electronics in
**TRUCK / AGRI / OHV**. A Device Under Test (DUT) can be an ECU, EGR/VGT
actuator, sensor, gateway or other electronic module. This is **not just an
automotive fault-code tester**.

## Current development workflow

For a live map of local worktrees, stacked PRs and CM5 deployment state,
see [repository workflow baseline](docs/REPOSITORY_WORKFLOW_2026-10-09.md).
Run `python3 scripts/ecu_repo_doctor.py status --github --siblings --cm5`
for read-only diagnostics, or `python3 scripts/ecu_repo_doctor.py check
--scope repo` for the repository itself. Choose `--scope sac` from a
SAC candidate checkout for rapid **offline** regression. This is available on
the workflow candidate branch; production `main` remains owner-gated.

## Layer boundaries

1. **CORE V2:** portable, DUT-neutral bus, protocol and safety primitives.
2. **Bench Runtime:** one DUT per laboratory session by default; resource
   ownership, lifecycle, watchdog, interlocks and safe-stop.
3. **DUT Profile:** physical links, protocol requirements and DUT-specific
   decoding, diagnostics or cyclic actuation. Proprietary EGR/VGT behavior
   does not belong in generic CORE.
4. **Application:** high-level, domain-aware operations and snapshots.
5. **Application API V1 (integration candidate):** separate authenticated,
   versioned read-only API and completed DTC/parameter snapshots.
   Hardware commands never transit directly through the browser.
6. **WebGUI/kiosk (integration candidate):** an untrusted presentation-only
   client using a strictly scoped localhost backend and systemd credentials.
   The prototype does not require an operator-typed browser token.
   See [WebGUI client-only contract](docs/WEBGUI_CLIENT_ONLY_ARCHITECTURE.md).

This candidate includes a deployed-and-tested CM5 WebGUI and restricted
SAC read-only connection adapter, but is **not yet merged to production
main**. A new physical parameter-lifecycle and DTC-clear acceptance gate
remains open; see
[operational release candidate](docs/OPERATIONAL_CANDIDATE_SCOPE_2026-10-09.md).
CM5 runtime adapters remain separate from multiplatform CORE V2.

## Build — portable C++ V2

Requires a C++17 toolchain and CMake 3.25+. Linux Ninja examples:

```sh
cmake -S . -B build/v2 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/v2
ctest --test-dir build/v2 --output-on-failure
```

The fresh **default configuration builds CORE V2 + Bench + DUT Profile +
DAF SAC Application**, and excludes the legacy Core, legacy SAC and legacy
SocketCAN adapter. A Linux build also includes the **separate Linux V2
platform adapter**. No executable is configured to contact a real ECU
automatically during ordinary CMake/CTest runs.

On Windows/MSVC, use CMake's Visual Studio generator. The Linux adapter is
excluded automatically and cannot be forced ON for a non-Linux target.
The source-level portable layers also support
`-DCMAKE_SYSTEM_NAME=Generic` for simulated conformance testing.

To build only CORE V2:

```sh
cmake -S . -B build/v2-core -G Ninja -DECU_BUILD_BENCH_RUNTIME=OFF
cmake --build build/v2-core --target ecu_core_v2_tests
```

Optional legacy code remains in the repository as an explicitly enabled,
isolated reference, not the default V2 execution path.

Verify freshly configured defaults with:

```sh
python3 scripts/check_v2_default_build.py build/v2 --system Linux
```

Accepted values for `--system`: `Linux`, `Windows`, `Generic`.
Do not use `--system Linux` to validate a Windows or Generic configuration.

## Engineering verification

```sh
bash scripts/validate_core_v2_foundation.sh
bash scripts/validate_bench_runtime_stage2.sh
bash scripts/validate_daf_sac_application.sh
```

The application gate includes Debug, Release, Generic, ASan/UBSan,
architecture checks, negative security tests and read-only simulation.
Existing GitHub CI covers Linux x86_64 GCC/Clang and Windows MSVC
x64/Win32 Debug/Release for CORE/Bench/DUT Profile. The expanded
DAF SAC **Application** and default-build CI matrix is proposed by the
full release-candidate branch and requires explicit GitHub workflow-write
permission before it can run remotely. The CM5 ARM64 build is validated
locally.

**All physical tests are separate, explicit operator actions.** In
particular, never execute the DTC-clear operator tools as part of a build,
CI run, release test or WebGUI startup. Previous physical proof archives
and consumed operator authorization must remain intact.

## Production gates

The repository's `main` branch is controlled by the project owner.
Auditing, preparing code, passing local tests and opening a PR do **not**
authorize merging into production. The release candidate must pass the
multi-OS CI matrix, architecture/security audits, and explicit owner
review before a concrete `main` merge.

See [project governance](docs/PROJECT_GOVERNANCE.md).
