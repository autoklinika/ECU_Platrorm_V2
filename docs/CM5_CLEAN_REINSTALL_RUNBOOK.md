# ECU Platform V2 — CM5 clean reinstall runbook

> Status: PRE-WIPE / install baseline
>
> Date frozen: 2026-10-06
>
> Purpose: rebuild the first ECU Platform V2 prototype on CM5 from a clean operating-system image. The legacy root filesystem is not reused.

## 1. Selected base operating system

**Raspberry Pi OS Lite (64-bit), Debian 13 (Trixie)**

Frozen reference image:

- Raspberry Pi official image
- release date: 2026-09-15
- architecture: arm64
- Debian: 13 (trixie)
- kernel family: 6.18
- desktop environment: none
- official image SHA-256: `cdf4f3bfac35ae947b46e4e767f935453810549779ac3290e05a6754aee627e5`

Rationale:

1. Official CM5 support and Raspberry Pi boot/firmware integration.
2. Native Raspberry Pi Device Tree overlay workflow for the current KAmod CAN-FD / MCP251xFD hardware.
3. The current CM5 hardware has already been validated with the Raspberry Pi 6.18 kernel family and `mcp251xfd`.
4. DRM/KMS, HDMI and Raspberry Pi hardware integration are available without installing a desktop environment.
5. Debian 13 provides the required standard server/development packages while keeping the OS small.
6. We do not need Ubuntu-specific features for Prototype A; reducing platform variance is more valuable at this stage.

Do not install Raspberry Pi OS Desktop or Full.

## 2. V2 UI baseline

ECU Platform V2 is **WebGUI-first**.

The CM5 does not run a separate Qt/QML user application. The local display runs the same WebGUI as a kiosk client.

Baseline display stack:

```text
Linux
  -> DRM/KMS
  -> Wayland
  -> Cage kiosk compositor
  -> Chromium in native Wayland kiosk mode
  -> ECU Platform WebGUI
```

No full desktop environment, panel, file manager or display manager is required.

The Debian 13 arm64 repositories contain both `cage` and `chromium`.

## 3. Service model target

```text
systemd
 |
 +-- ecu-platform-core.service
 |     +-- domain/application logic
 |     +-- transport abstraction
 |     +-- CAN/CAN-FD
 |     +-- ISO-TP / UDS / J1939
 |     +-- DoIP
 |     +-- actuator runtime / safety
 |     +-- logging / replay
 |
 +-- ecu-platform-web.service
 |     +-- API
 |     +-- WebGUI assets/backend
 |
 +-- ecu-platform-kiosk.service
       +-- Cage
       +-- Chromium
       +-- local WebGUI
```

The exact backend and frontend technology stack is intentionally not frozen by this reinstall runbook.

## 4. Hardware configuration to restore after clean install

### CAN-FD

Current verified hardware:

- KAmod CAN-FD
- Linux driver: `mcp251xfd`
- interface: `can0`
- SPI parent: `spi0.0`
- oscillator: 40 MHz
- interrupt GPIO: 25

Required boot configuration:

```text
dtparam=spi=on
dtoverlay=mcp251xfd,spi0-0,oscillator=40000000,interrupt=25
dtoverlay=spi-bcm2835
```

CAN-FD validation must verify `struct canfd_frame`/64-byte operation, not only legacy CAN frames.

### Cooling

Restore the verified CM5 fan profile documented in `CM5_PREWIPE_BASELINE_2026-10-06.md`.

### Display / touch

- HDMI display
- verified modes include 1280x720 and 1920x1080
- WaveShare USB HID touch/input device, USB ID `0712:0009`

## 5. Base installation configuration

During imaging:

- hostname: `ecu`
- create the administrative/development user explicitly
- enable SSH
- configure timezone/locale
- configure network needed for first boot
- do not store Wi-Fi passwords, SSH private keys or other credentials in GitHub

After first boot:

1. Update the system.
2. Validate kernel/firmware and CM5 identity.
3. Validate networking and SSH.
4. Restore CAN-FD overlay and validate `can0`.
5. Restore fan configuration.
6. Install remote administration components.
7. Install kiosk graphics stack.
8. Create V2 runtime/service users and directories.
9. Deploy first Core/API/WebGUI skeleton.
10. Enable kiosk only after API/WebGUI health checks pass.

## 6. Base packages — first pass

System/development:

- `git`
- `build-essential`
- `cmake`
- `ninja-build`
- `pkg-config`
- `curl`
- `ca-certificates`
- `jq`
- `zstd`

Hardware/network:

- `can-utils`
- `network-manager`
- `openssh-server`

Kiosk:

- `cage`
- `chromium`

Remote maintenance:

- Tailscale
- Node.js 22 LTS or newer compatible with the selected Desktop Commander version
- Remote Desktop Commander as a persistent systemd user/service installation

Do not install a desktop metapackage.

## 7. Security/operational rules

- Core owns hardware; WebGUI never opens CAN devices directly.
- WebGUI sends high-level commands through the application API.
- Do not run Core as root merely to simplify hardware access.
- Grant only the minimum Linux groups/capabilities required by hardware adapters.
- Remote access is not a substitute for product authentication.
- Tailscale and Desktop Commander are development/maintenance facilities for Prototype A, not yet part of the commercial product contract.
- Secrets and remote-access state are reprovisioned after reinstall and are never committed.

## 8. Legacy migration rule

Legacy `autoklinika/ecu_platform` is a read-only evidence/reference source.

Allowed reuse after review:

- validated protocol facts,
- CAN IDs and addressing,
- DID/DTC knowledge,
- timing parameters,
- hardware behavior,
- test results,
- regression evidence,
- isolated algorithms/implementation techniques when justified.

Forbidden approach:

- copying the old application structure,
- copying old GUI control flow,
- preserving old GUI-to-hardware ownership,
- mechanically porting the legacy program and calling it V2.

## 9. Pre-wipe dependencies

Evidence release:

- private repository: `autoklinika/ecu_platform`
- release/tag: `prewipe-evidence-2026-10-06`
- archive SHA-256: `a2edd5394b9d09ffc2c98b98c558c22cfaa6e6dee399e673b278b691b0ea30de`
- GitHub restore verification: PASS

Before actually wiping eMMC:

- [x] source/reference branches are on GitHub
- [x] stable v1.8 is on GitHub
- [x] hardware baseline is documented
- [x] evidence archive uploaded to private GitHub release
- [x] evidence SHA-256 recorded
- [x] archive downloaded back from GitHub and verified
- [x] base OS selected and pinned
- [x] WebGUI kiosk direction selected
- [ ] final imaging procedure validated against the physical CM5 carrier / USB-boot method
- [ ] operator confirms destructive wipe

No destructive storage operation is performed by this runbook until the final two gates are closed.
