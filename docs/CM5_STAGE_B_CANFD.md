# ECU Platform V2 — CM5 Stage B CAN-FD

## Hardware

Prototype A uses the existing KAmod CAN-FD module:

- controller family: MCP251xFD
- Linux driver: `mcp251xfd`
- SPI: `spi0.0`
- oscillator: 40 MHz
- interrupt GPIO: 25
- Linux interface target: `can0`

## Stage B1 — boot configuration

Run:

```bash
chmod +x ~/bootstrap_cm5_stage_b_canfd.sh
~/bootstrap_cm5_stage_b_canfd.sh
sudo reboot
```

The script is idempotent and creates a timestamped backup of `/boot/firmware/config.txt` before first modification.

## Stage B2 — validation after reboot

Run:

```bash
chmod +x ~/validate_cm5_stage_b_canfd.sh
~/validate_cm5_stage_b_canfd.sh
```

Validation:

1. verifies `can0` exists,
2. records driver/module state,
3. configures a temporary CAN-FD profile:
   - arbitration bitrate: 500 kbit/s
   - data bitrate: 2 Mbit/s
   - FD: on
4. verifies that the kernel reports CAN-FD enabled,
5. returns `can0` to DOWN,
6. transmits **no frames**.

The temporary bitrate values are only a capability smoke test. ECU-specific bit timing belongs to runtime configuration/transport adapters and is not hard-coded into Core.

Expected final marker:

```text
STAGE_B_CANFD=PASS
```

## Gate

Stage B is VERIFIED only after the physical CM5 produces the PASS marker.


## Physical validation — 2026-10-06

Result: **PASS**

Observed on clean CM5 after reboot:

- kernel module: `mcp251xfd`
- `can0` present on `spi0.0`
- controller clock: `40000000`
- classic CAN timing capability exposed
- CAN-FD data-phase timing capability exposed
- temporary capability profile accepted:
  - arbitration bitrate: `500000`
  - data bitrate: `2000000`
- interface MTU changed from `16` to `72`
- kernel reported: `can <FD,TDC-AUTO>`
- state after enabling: `ERROR-ACTIVE`
- TX/RX bus error counters: `0 / 0`
- no CAN frames transmitted by the validation procedure

The first validator revision incorrectly searched for the literal text `fd on`. On Debian 13 / iproute2 6.15 the active mode is rendered as `can <FD,TDC-AUTO>`. The validator was corrected to check the actual kernel/iproute2 representation plus `dbitrate` and MTU 72.
