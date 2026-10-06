# ECU Platform V2

ECU Platform V2 is a WebGUI-first ECU diagnostic/service platform whose Core is designed to remain independent of the current OS and hardware.

## Current bootstrap line

Active development/bootstrap branch:

```text
setup/cm5-bootstrap
```

Production `main` is user-controlled and must not be changed without explicit approval.

## Prototype A

Current reference platform:

- Raspberry Pi Compute Module 5
- Raspberry Pi OS Lite 64-bit / Debian 13
- KAmod CAN-FD / MCP251xFD
- DRM/KMS + Wayland/Cage + Chromium kiosk

Prototype A is not an architectural dependency of Core.

## Build the current Core bootstrap

```bash
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure
```

Full Stage E gate:

```bash
./scripts/validate_stage_e_core_bootstrap.sh
```

See `docs/ZALOZENIA_ROBOCZE.md` and `docs/PROJECT_GOVERNANCE.md` for mandatory architecture and merge rules.
