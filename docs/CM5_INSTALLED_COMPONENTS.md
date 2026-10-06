# ECU Platform V2 — CM5 installed components register

> Branch: `setup/cm5-bootstrap`
>
> Purpose: single source of truth for packages, services and configuration installed on the clean CM5 Prototype A.
>
> Rule: every package/service added to the prototype must be recorded here in the same change that introduces it.

## Status legend

- `PLANNED` — approved for installation, not yet verified on the clean CM5
- `INSTALLED` — installed on the clean CM5
- `VERIFIED` — installed and functionally validated
- `DEFERRED` — intentionally postponed until the architecture/component choice is frozen

## 1. Base operating system

| Component | Version / source | Purpose | Status |
|---|---|---|---|
| Raspberry Pi OS Lite 64-bit | Debian 13 Trixie, kernel `6.18.50+rpt-rpi-2712` | Base OS for Prototype A | VERIFIED |
| systemd | distribution package | Service supervision | INSTALLED |
| OpenSSH server | OpenSSH `10.0p2` Debian `7+deb13u4` | Remote administration / VS Code Remote SSH | VERIFIED |
| NetworkManager | distribution package, enabled + active | Ethernet/Wi-Fi configuration | VERIFIED |

## 2. Base administration and diagnostics

| Package | Purpose | Status |
|---|---|---|
| `git` | Source control — `2.47.3` | VERIFIED |
| `curl` | HTTPS/bootstrap downloads — `8.14.1-2+deb13u5` | VERIFIED |
| `ca-certificates` | TLS CA store — `20250419` | VERIFIED |
| `jq` | JSON diagnostics/scripts — `1.7.1-6+deb13u4` | VERIFIED |
| `zstd` | Evidence/log compression — `1.5.7+dfsg-1` | VERIFIED |
| `rsync` | Controlled file synchronization — `3.5.0+ds1-0+deb13u1` | VERIFIED |
| `usbutils` | USB diagnostics (`lsusb`) — `1:018-2` | VERIFIED |
| `pciutils` | PCIe diagnostics (`lspci`) — `1:3.13.0-2` | VERIFIED |
| `ethtool` | Ethernet diagnostics — `1:6.14.2-1` | VERIFIED |
| `gpiod` | GPIO diagnostics — `2.2.1-2+deb13u1` | VERIFIED |
| `i2c-tools` | I2C diagnostics for future replaceable peripherals — `4.4-2` | VERIFIED |

## 3. C++ development/build baseline

| Package | Purpose | Status |
|---|---|---|
| `build-essential` | GCC/G++ and base build tools; G++ `14.2.0` | VERIFIED |
| `cmake` | C++ build configuration — `3.31.6` | VERIFIED |
| `ninja-build` | CMake build backend — `1.12.1` | VERIFIED |
| `pkg-config` | Native library discovery — `1.8.1-4` | VERIFIED |

**Invariant:** installing the toolchain does not make Linux or Raspberry Pi part of Core. Platform-specific code stays behind adapters.

## 4. CAN / CAN-FD

| Component | Purpose | Status |
|---|---|---|
| `can-utils` | CAN/CAN-FD diagnostics and smoke tests — `2023.03-1+b2` | VERIFIED |
| kernel `mcp251xfd` driver | KAmod CAN-FD / MCP251xFD; SPI0.0, 40 MHz | VERIFIED |
| Device Tree overlay | SPI0.0, 40 MHz oscillator, IRQ GPIO25 | VERIFIED |

Required overlay:

```text
dtparam=spi=on
dtoverlay=mcp251xfd,spi0-0,oscillator=40000000,interrupt=25
dtoverlay=spi-bcm2835
```

Validation must cover classic CAN **and CAN-FD**. A legacy 8-byte-only test is insufficient.

## 5. Remote development / maintenance

| Component | Source | Purpose | Status |
|---|---|---|---|
| Tailscale | official Tailscale repository/installer | Stable remote network access | PLANNED |
| Node.js 22 LTS+ | NodeSource | Runtime required by current Remote Desktop Commander | PLANNED |
| Remote Desktop Commander | `@wonderwhy-er/desktop-commander` | MCP access to Prototype A | PLANNED |

Tailscale and Remote Desktop Commander are **development/maintenance tooling for Prototype A**, not product dependencies of ECU Platform Core.

## 6. WebGUI kiosk baseline

| Component | Purpose | Status |
|---|---|---|
| `cage` | Minimal Wayland kiosk compositor | PLANNED |
| `chromium` | Local WebGUI kiosk client | PLANNED |

No desktop environment, display manager, panel or file manager is part of the V2 baseline.

Target local UI chain:

```text
DRM/KMS -> Wayland -> Cage -> Chromium kiosk -> ECU Platform WebGUI
```

## 7. Intentionally not installed yet

| Component | Reason | Status |
|---|---|---|
| Qt/QML application stack | V2 is WebGUI-first | DEFERRED |
| React/Vue/Svelte/etc. | WebGUI frontend stack not yet selected | DEFERRED |
| C++ HTTP/WebSocket framework | API/backend framework not yet selected | DEFERRED |
| Database engine | Persistence model not yet frozen | DEFERRED |
| Docker/Podman | Avoid adding runtime indirection before architecture requires it | DEFERRED |

## 8. Installation policy

1. Install only components listed here or added through a reviewed branch change.
2. Record exact installed versions after each stage.
3. Do not install a package merely because legacy ECU Platform used it.
4. Do not copy legacy runtime configuration blindly.
5. Do not merge this setup branch to production `main` without explicit user approval.


## 9. Stage A verification — 2026-10-06

Result: **PASS**

Observed on clean CM5:

- kernel: `6.18.50+rpt-rpi-2712`
- architecture: `aarch64`
- Git: `2.47.3`
- CMake: `3.31.6`
- Ninja: `1.12.1`
- G++: `14.2.0`
- iproute2: `6.15.0`
- OpenSSH: `10.0p2 Debian-7+deb13u4`
- SSH service: enabled + active
- NetworkManager service: enabled + active

Note: the bootstrap's `candump --help` probe emitted `candump: invalid option -- '-'`. This is a bootstrap-script probe issue only; package `can-utils 2023.03-1+b2` installed successfully. CAN/CAN-FD functionality is intentionally not marked VERIFIED until Stage B hardware validation.


## 10. Stage B verification — 2026-10-06

Result: **PASS**

Physical validation confirmed KAmod CAN-FD / MCP251xFD on the clean CM5:

- driver `mcp251xfd`
- parent `spi0.0`
- clock `40 MHz`
- `can0` present
- CAN-FD MTU `72`
- temporary profile `500 kbit/s / 2 Mbit/s` accepted
- kernel mode `<FD,TDC-AUTO>`
- error counters `tx 0 / rx 0`
- no traffic transmitted during the capability probe
