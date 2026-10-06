# CM5 pre-wipe baseline — 2026-10-06

Status: pre-reinstall inventory for ECU Platform V2.  
Rule: do not wipe the CM5 until the evidence archive is stored outside the device and verified.

## 1. Current hardware baseline

- Host: Raspberry Pi Compute Module 5 Rev 1.0
- CPU: 4× Cortex-A76, max 2.4 GHz
- RAM: 8 GiB
- Storage: 64 GB eMMC (58.2 GiB visible)
- Current OS: Debian GNU/Linux 13 (trixie), aarch64
- Kernel observed: 6.18.50+rpt-rpi-2712
- Display: HDMI-A-1 connected; modes include 1280×720 and 1920×1080
- Touch/input: WaveShare USB HID, USB ID 0712:0009
- Network: Ethernet + Wi-Fi
- Current local GUI stack: Qt 6 / QML / Wayland desktop environment

## 2. CAN / CAN-FD baseline

Physical module: KAmod CAN-FD based on MCP251xFD family.

Observed Linux interface:

- interface: `can0`
- driver: `mcp251xfd`
- parent bus: SPI
- parent device: `spi0.0`
- oscillator: 40 MHz
- classic CAN and CAN-FD capability exposed by the driver

Current boot configuration relevant to CAN:

```text
dtparam=spi=on
dtoverlay=mcp251xfd,spi0-0,oscillator=40000000,interrupt=25
dtoverlay=spi-bcm2835
```

The existing legacy application currently uses classic Linux `struct can_frame` and therefore does not yet use the full CAN-FD capability of the hardware.

## 3. Cooling baseline

Current boot configuration contains the ECU Platform fan profile:

```text
dtparam=cooling_fan=on
dtparam=fan_temp0=45000
dtparam=fan_temp0_hyst=3000
dtparam=fan_temp0_speed=70
dtparam=fan_temp1=55000
dtparam=fan_temp1_hyst=3000
dtparam=fan_temp1_speed=120
dtparam=fan_temp2=65000
dtparam=fan_temp2_hyst=4000
dtparam=fan_temp2_speed=190
dtparam=fan_temp3=75000
dtparam=fan_temp3_hyst=5000
dtparam=fan_temp3_speed=255
```

## 4. Current software baseline worth preserving as reference only

Legacy repository: `autoklinika/ecu_platform`.

Stable release verified on the device:

- branch: `release/v1.8`
- commit: `99a66fc82d39acf10acb456adac3983e02039113`
- local source tree matches `origin/release/v1.8`; only generated/build files are dirty

Important legacy knowledge exists on remote branches, including:

- Actuator Engine Stage 0–10
- MAN Sonceboz EGR protocol/control/autotest/resolution/report work
- AirModule / ETC3 proof of concept
- SAC / MCM / CAN Scanner / ISO-TP / UDS work

The Stage 10 branch contains extensive architecture, implementation and hardware-validation documentation under `docs/` and `docs/reports/`.

## 5. Pre-wipe evidence archive

The real CM5 validation evidence has been archived and stored on GitHub in the **private** legacy repository `autoklinika/ecu_platform` as release:

- tag: `prewipe-evidence-2026-10-06`
- release title: `ECU Platform pre-wipe evidence 2026-10-06`
- archive: `ecu-platform-prewipe-evidence-2026-10-06.tar.zst`
- archive size: 12,711,331 bytes
- archive SHA-256: `a2edd5394b9d09ffc2c98b98c558c22cfaa6e6dee399e673b278b691b0ea30de`
- archived entries: 331
- archive test: PASS

The release also contains:

- `ecu-platform-prewipe-evidence-2026-10-06.manifest.sha256`
- `ecu-platform-prewipe-evidence-2026-10-06.index.txt`

The archive contains the pre-wipe contents of:

- `/home/ecu/ecu_logs`
- `/home/ecu/ecu_reports`
- `/home/ecu/ecu_backup`
- `/home/ecu/ecu_platform_backup_untracked`

The archive was copied from CM5 to AI Server over the private Tailscale network and verified there before GitHub upload. The SHA-256 calculated after transfer matched the SHA-256 calculated on CM5. A basic text scan found no password/token/private-key patterns in the archived evidence.

The raw evidence is intentionally stored in the private legacy repository rather than the public V2 repository.

## 6. Local runtime settings to preserve as knowledge, not as secrets

Current ECU Platform operator settings include:

- language: Polish
- AirModule Ethernet interface: `eth0`
- AirModule Ethernet platform address: `192.168.50.1`
- AirModule Ethernet module address: `192.168.50.2`
- AirModule Wi-Fi interface: `wlan0`
- display brightness: 100%
- auto-dim disabled
- night mode disabled

Credentials, authentication tokens, Tailscale state and Remote Desktop Commander device credentials must NOT be committed.

## 7. Rebuild policy accepted for ECU Platform V2

The clean rebuild after reinstall follows these rules:

1. Install a minimal Linux without a desktop environment.
2. ECU Platform V2 uses **WebGUI as its user interface**.
3. The local CM5 display runs a minimal kiosk browser/client that opens the locally served WebGUI; there is no separate Qt/QML application UI in the V2 baseline.
4. The same WebGUI/application API model must support local kiosk use and authorized remote clients without duplicating control logic.
5. Build ECU Platform V2 from a new architecture and current requirements.
6. Treat legacy `ecu_platform` as a read-only knowledge/reference source.
7. Do not mechanically copy legacy directory structure or application control flow.
8. Reuse protocol facts, validated constants, hardware behavior and selected implementation techniques only after review.
9. Core must be independent from WebGUI, Linux-specific APIs and the current CM5 hardware.
10. CAN/CAN-FD, DoIP and future transports must sit behind stable transport/platform interfaces.
11. WebGUI is a client of Core/API and cannot own hardware or diagnostic control logic.
12. Qt/QML is not part of the V2 baseline UI architecture.
13. Hardware evidence and regression fixtures should be preserved so the new implementation can be validated against known behavior.

## 8. Pre-wipe gate

The CM5 may be wiped only after all of the following are true:

- [x] legacy source branches required as reference exist on GitHub
- [x] stable v1.8 release exists on GitHub
- [x] CM5 hardware/CAN/display/cooling baseline documented
- [x] raw EGR/Actuator evidence archived outside the CM5
- [x] evidence archive checksum recorded
- [x] evidence archive copied to private GitHub-backed storage
- [ ] reinstall/runbook for the minimal kiosk image prepared and reviewed
