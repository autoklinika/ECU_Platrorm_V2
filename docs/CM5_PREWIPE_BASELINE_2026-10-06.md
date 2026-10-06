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

## 5. Local evidence not yet present on GitHub

These files are currently present only on the CM5 and must not be lost before wipe:

- `/home/ecu/ecu_logs`: about 249 MB, 24 files
- `/home/ecu/ecu_reports`: about 5.6 MB, 221 files
- `/home/ecu/ecu_backup`: 2 small historical files
- `/home/ecu/ecu_platform_backup_untracked`: 2 historical ECU_Factory files

Largest evidence file:

- `/home/ecu/ecu_logs/sonceboz_egr_raw.log`: about 260 MB uncompressed
- observed compressed size at zstd level 3: about 11.4 MB

GitHub code search did not find the raw Sonceboz log, timing snapshots, autotest JSON files or resolution-test JSON files.

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
2. Run the product in kiosk mode with only the graphics/input components required by the application.
3. Build ECU Platform V2 from a new architecture and current requirements.
4. Treat legacy `ecu_platform` as a read-only knowledge/reference source.
5. Do not mechanically copy legacy directory structure or application control flow.
6. Reuse protocol facts, validated constants, hardware behavior and selected implementation techniques only after review.
7. Core must be independent from GUI, Linux-specific APIs and the current CM5 hardware.
8. CAN/CAN-FD, DoIP and future transports must sit behind stable transport/platform interfaces.
9. Qt/QML is a client/UI technology, not part of Core.
10. Hardware evidence and regression fixtures should be preserved so the new implementation can be validated against known behavior.

## 8. Pre-wipe gate

The CM5 may be wiped only after all of the following are true:

- [x] legacy source branches required as reference exist on GitHub
- [x] stable v1.8 release exists on GitHub
- [x] CM5 hardware/CAN/display/cooling baseline documented
- [ ] raw EGR/Actuator evidence archived outside the CM5
- [ ] evidence archive checksum recorded
- [ ] evidence archive copied to its final GitHub-backed storage or another durable project archive
- [ ] reinstall/runbook for the minimal kiosk image prepared and reviewed
