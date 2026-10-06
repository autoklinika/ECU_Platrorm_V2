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
| Raspberry Pi OS Lite 64-bit | Debian 13 Trixie, official Raspberry Pi image | Base OS for Prototype A | INSTALLED |
| systemd | distribution package | Service supervision | INSTALLED |
| OpenSSH server | distribution package | Remote administration / VS Code Remote SSH | VERIFIED |
| NetworkManager | distribution package | Ethernet/Wi-Fi configuration | INSTALLED |

## 2. Base administration and diagnostics

| Package | Purpose | Status |
|---|---|---|
| `git` | Source control | PLANNED |
| `curl` | HTTPS/bootstrap downloads | PLANNED |
| `ca-certificates` | TLS CA store | PLANNED |
| `jq` | JSON diagnostics/scripts | PLANNED |
| `zstd` | Evidence/log compression | PLANNED |
| `rsync` | Controlled file synchronization | PLANNED |
| `usbutils` | USB diagnostics (`lsusb`) | PLANNED |
| `pciutils` | PCIe diagnostics (`lspci`) | PLANNED |
| `ethtool` | Ethernet diagnostics | PLANNED |
| `gpiod` | GPIO diagnostics | PLANNED |
| `i2c-tools` | I2C diagnostics for future replaceable peripherals | PLANNED |

## 3. C++ development/build baseline

| Package | Purpose | Status |
|---|---|---|
| `build-essential` | GCC/G++ and base build tools | PLANNED |
| `cmake` | C++ build configuration | PLANNED |
| `ninja-build` | CMake build backend | PLANNED |
| `pkg-config` | Native library discovery | PLANNED |

**Invariant:** installing the toolchain does not make Linux or Raspberry Pi part of Core. Platform-specific code stays behind adapters.

## 4. CAN / CAN-FD

| Component | Purpose | Status |
|---|---|---|
| `can-utils` | CAN/CAN-FD diagnostics and smoke tests | PLANNED |
| kernel `mcp251xfd` driver | KAmod CAN-FD / MCP251xFD | PLANNED |
| Device Tree overlay | SPI0.0, 40 MHz oscillator, IRQ GPIO25 | PLANNED |

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
