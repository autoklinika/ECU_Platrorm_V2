# ECU Platform V2 — CM5 Stage D minimal WebGUI kiosk

## Scope

Stage D adds only the local display runtime for **Prototype A**:

```text
Raspberry Pi OS Lite
  -> DRM/KMS
  -> Wayland compositor (Cage)
  -> Chromium/Ozone Wayland kiosk
  -> ECU Platform WebGUI URL
```

This stage does **not** select a frontend framework, HTTP/WebSocket framework, database, container runtime or Core architecture.

All files introduced here are deployment/runtime concerns for Prototype A. ECU Platform Core must not depend on Raspberry Pi OS, systemd, DRM/KMS, Cage or Chromium.

## Design choices

- Existing kernel KMS driver: `vc4-kms-v3d`.
- Cage is launched directly on a virtual terminal by a system service.
- Cage uses the systemd/logind session path through a dedicated PAM stack.
- No desktop environment and no display manager are installed.
- No separate `seatd` daemon is required for this baseline.
- Chromium is forced to native Wayland with `--ozone-platform=wayland`.
- Chromium sandbox remains enabled; `--no-sandbox` is not used.
- XWayland is not installed by Stage D.
- The initial URL is a static local placeholder so Stage D does not implicitly choose the future WebGUI or backend stack.

## Preflight on clean CM5 — 2026-10-06

Observed before installation:

- kernel: `6.18.50+rpt-rpi-2712`
- `/dev/dri/card0`, `card1`, `renderD128`: present
- boot config contains `dtoverlay=vc4-kms-v3d`
- `systemd-logind.service`: active
- user `ecu` belongs to `video`, `render` and `input`
- `chvt`: `/usr/bin/chvt`
- package candidates:
  - Cage: `0.3.1-1~bpo13+1+rpt2`
  - Chromium: `1:154.0.8037.92-1~deb13u1+rpt1`
  - rpi-chromium-mods: `20260211`

Status: **PREPARED** before installation; installation subsequently completed successfully on the clean CM5.

## Installation

The bootstrap is intentionally root-gated so privilege is requested once:

```bash
cd ~/ECU_Platrorm_V2
git pull --ff-only origin setup/cm5-bootstrap
sudo ./scripts/bootstrap_cm5_stage_d_kiosk.sh
```

The script installs:

- `cage`
- `chromium`
- `rpi-chromium-mods`

using `--no-install-recommends` to avoid pulling a desktop environment.

It also creates:

- `/usr/local/libexec/ecu-platform/ecu-kiosk-launcher`
- `/opt/ecu-platform/kiosk/stage-d.html`
- `/etc/default/ecu-kiosk`
- `/etc/pam.d/ecu-kiosk`
- `/etc/systemd/system/ecu-kiosk.service`

and sets `graphical.target` as the default boot target.

## URL contract

The kiosk endpoint is deployment configuration, not Core logic:

```text
/etc/default/ecu-kiosk
ECU_KIOSK_URL=file:///opt/ecu-platform/kiosk/stage-d.html
```

When the real ECU Platform WebGUI exists, Stage D should require only changing `ECU_KIOSK_URL` and restarting `ecu-kiosk.service`.

## Validation

After installation:

```bash
./scripts/validate_cm5_stage_d_kiosk.sh
```

Automated validation checks:

- DRM/KMS nodes and boot configuration
- installed package versions
- kiosk service enabled + active
- `graphical.target` default
- Cage -> Chromium process chain
- Chromium `--ozone-platform=wayland`
- Chromium `--kiosk`
- Wayland socket
- absence of an XWayland process
- external kiosk URL configuration

Expected marker:

```text
STAGE_D_KIOSK=PASS
```

## Final Stage D gate

Stage D is not **VERIFIED** until all of the following are confirmed after one reboot:

1. CM5 boots directly into the Cage/Chromium kiosk without a desktop environment.
2. The placeholder page is visible on the HDMI display.
3. WaveShare USB HID touch still reaches Chromium.
4. SSH, Tailscale and Remote Desktop Commander recover normally.
5. `scripts/validate_cm5_stage_d_kiosk.sh` returns `STAGE_D_KIOSK=PASS`.

Do not merge `setup/cm5-bootstrap` to production `main` as part of Stage D without explicit user approval.


## Pre-reboot validation — 2026-10-06

Result: **PASS**

Observed after running the Stage D bootstrap:

- `cage 0.3.1-1~bpo13+1+rpt2`
- `chromium 1:154.0.8037.92-1~deb13u1+rpt1`
- `rpi-chromium-mods 20260211`
- `ecu-kiosk.service`: enabled + active
- default target: `graphical.target`
- active logind kiosk session: user `ecu`, `seat0`, `tty1`
- Wayland socket: `/run/user/1000/wayland-0`
- Chromium launched by Cage with `--ozone-platform=wayland --kiosk`
- no XWayland process
- HDMI connector `card1-HDMI-A-1`: connected
- WaveShare USB HID input device enumerated as `WaveShare WaveShare`
- Tailscale remained active at `100.92.219.91`
- `desktop-commander-remote.service`: enabled + active
- automated marker: `STAGE_D_KIOSK=PASS`

Stage D remains **PENDING REBOOT / PHYSICAL TOUCH VALIDATION**. The final gate still requires proving automatic kiosk recovery after reboot and confirming touch interaction on the local display.
