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
| Tailscale | official Tailscale installer; service enabled + active; reboot recovery verified | Stable remote network access | VERIFIED |
| Node.js 22 LTS+ | NodeSource; `v22.23.3`, npm `10.9.9` | Runtime required by current Remote Desktop Commander | VERIFIED |
| Remote Desktop Commander | `@wonderwhy-er/desktop-commander`, app `0.2.52`; persistent user service; reboot recovery verified | MCP access to Prototype A | VERIFIED |

Tailscale and Remote Desktop Commander are **development/maintenance tooling for Prototype A**, not product dependencies of ECU Platform Core.

## 6. WebGUI kiosk baseline

| Component | Version / source | Purpose | Status |
|---|---|---|---|
| DRM/KMS / `vc4-kms-v3d` | kernel + Raspberry Pi boot configuration | Prototype A local display backend | VERIFIED |
| `cage` | `0.3.1-1~bpo13+1+rpt2` | Minimal Wayland kiosk compositor | VERIFIED |
| `chromium` | `1:154.0.8037.92-1~deb13u1+rpt1` | Local WebGUI kiosk client using native Wayland/Ozone | VERIFIED |
| `rpi-chromium-mods` | `20260211` | Raspberry Pi-specific Chromium runtime settings; Prototype A only | VERIFIED |
| `ecu-kiosk.service` | project-managed systemd unit; reboot recovery verified | Boot-time Cage/Chromium session on tty1 | VERIFIED |

No desktop environment, display manager, panel, file manager, XWayland or separate seat daemon is part of the Stage D baseline.

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

## 11. Stage C preparation — remote maintenance

Status: **PLANNED / scripts ready**

Prepared artifacts:

- `scripts/bootstrap_cm5_stage_c_remote.sh`
- `scripts/finalize_cm5_stage_c_remote.sh`
- `scripts/validate_cm5_stage_c_remote.sh`
- `docs/CM5_STAGE_C_REMOTE.md`

Target components:

- Tailscale — official Linux installer
- Node.js 22 LTS+ — NodeSource repository
- Remote Desktop Commander — npm package `@wonderwhy-er/desktop-commander`
- persistent `systemd --user` service
- Tailscale operator permission for user `ecu`
- user linger enabled for reboot-safe MCP service

Stage C remains unverified until both Tailscale and Remote Desktop Commander reconnect after reboot without a manually open terminal.

## 12. Stage C pre-reboot verification — 2026-10-06

Result: **PASS (pre-reboot)**

Independent verification through the new CM5 MCP endpoint confirmed:

- fresh CM5 MCP device online
- Tailscale service: enabled + active
- Tailscale address: `100.92.219.91`
- tailnet device name currently rendered as `ecu-1` because the pre-wipe `ecu` node still exists offline
- Node.js: `v22.23.3`
- npm: `10.9.9`
- Remote Desktop Commander app: `0.2.52`
- `desktop-commander-remote.service`: enabled + active
- user `ecu`: `Linger=yes`
- Desktop Commander runs from the persistent user service, not from a manually open `npx` terminal

Final Stage C status remains **PENDING REBOOT VALIDATION**. After one controlled CM5 reboot, Tailscale and Remote Desktop Commander must reconnect automatically without a manually open terminal.

## 13. Stage C final reboot validation — 2026-10-06

Result: **VERIFIED / PASS**

Controlled reboot validation confirmed automatic recovery without manually starting any terminal process:

- boot time observed: `2026-10-06 13:20:59`
- `tailscaled.service`: enabled + active after reboot
- Tailscale IP preserved: `100.92.219.91`
- Desktop Commander user service: enabled + active after reboot
- user `ecu`: `Linger=yes`
- persistent Desktop Commander process started automatically from `systemd --user`
- fresh CM5 MCP endpoint became reachable automatically from ChatGPT

Stage C is complete.

## 14. Stage D preflight — minimal kiosk

Status: **PREPARED / not installed yet**

Preflight on the clean CM5 confirmed:

- `vc4-kms-v3d` already configured
- DRM nodes `card0`, `card1`, `renderD128` present
- `systemd-logind` active
- user `ecu` already has `video`, `render` and `input` groups
- Cage, Chromium and Raspberry Pi Chromium modifiers are available from the configured Trixie repositories
- the current remote MCP session does not have passwordless sudo; the root-gated installation therefore requires one local administrator authentication

Prepared artifacts:

- `scripts/bootstrap_cm5_stage_d_kiosk.sh`
- `scripts/validate_cm5_stage_d_kiosk.sh`
- `docs/CM5_STAGE_D_KIOSK.md`

No Stage D component is marked INSTALLED or VERIFIED until the bootstrap and validation have actually run.


## 15. Stage D pre-reboot validation — 2026-10-06

Result: **PASS (pre-reboot)**

Independent validation through the CM5 MCP endpoint confirmed:

- Stage D bootstrap marker: `STAGE_D_INSTALL=PASS`
- Stage D validation marker: `STAGE_D_KIOSK=PASS`
- Cage, Chromium and `rpi-chromium-mods` installed at the versions recorded above
- `ecu-kiosk.service`: enabled + active
- `graphical.target`: default
- active `seat0/tty1` logind session for the kiosk
- native Wayland socket `/run/user/1000/wayland-0`
- Chromium using `--ozone-platform=wayland` and `--kiosk`
- no XWayland process
- HDMI-A-1 reported connected
- WaveShare USB HID touch device present in the input subsystem
- Tailscale remained active
- Remote Desktop Commander user service remained enabled + active

Final Stage D status remains **PENDING REBOOT + PHYSICAL TOUCH VALIDATION**.


## 16. Stage D reboot validation — 2026-10-06

Result: **PASS (technical reboot gate)**

After controlled reboot:

- boot timestamp: `2026-10-06 13:39:28 CEST`
- kiosk service automatically started at `13:39:34 CEST`
- `STAGE_D_KIOSK=PASS`
- native Wayland/Cage/Chromium chain restored
- `seat0/tty1` kiosk session restored
- no XWayland process
- HDMI-A-1 connected
- WaveShare HID present
- Tailscale automatically recovered
- Remote Desktop Commander automatically recovered

Software and reboot behavior are VERIFIED. Final Stage D acceptance awaits only local confirmation that the page is visibly rendered and the physical WaveShare touch interaction works.


## 17. Stage D physical touch acceptance — 2026-10-06

Result: **VERIFIED / PASS**

Touch input was validated beyond device enumeration:

- WaveShare HID classified by udev as `ID_INPUT_TOUCHSCREEN=1`
- raw `/dev/input/event1` capture produced 200 valid absolute/multitouch events
- Cage/Wayland session was active on `seat0`
- an isolated temporary Chromium page running on the live Wayland session received DOM pointer events
- browser-side test requests contained changing X/Y coordinates from real finger touches
- temporary test processes and files were removed afterward
- the normal Stage D kiosk remained active

Therefore the full display/input path is verified:

```text
DRM/KMS -> Cage/Wayland -> Chromium
WaveShare HID -> Linux input -> Cage/Wayland -> Chromium DOM
```

**Stage D is complete.**
