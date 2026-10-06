# ECU Platform V2 — CM5 Stage C remote maintenance

## Scope

Stage C restores development/maintenance access to the freshly installed Prototype A:

1. Tailscale
2. Node.js 22+
3. Remote Desktop Commander
4. persistent user service for Remote Desktop Commander
5. Tailscale operator permission for user `ecu`

These components are **Prototype A development/maintenance tools**. They are not part of ECU Platform Core and do not become commercial product dependencies.

## C1 — install

Run:

```bash
curl -fsSL \
https://raw.githubusercontent.com/autoklinika/ECU_Platrorm_V2/setup/cm5-bootstrap/scripts/bootstrap_cm5_stage_c_remote.sh \
-o ~/bootstrap_cm5_stage_c_remote.sh

chmod +x ~/bootstrap_cm5_stage_c_remote.sh
~/bootstrap_cm5_stage_c_remote.sh
```

The script uses the official Tailscale Linux installer and the NodeSource Node.js 22 repository.

## C1.5 — one-time authentication

Authenticate Tailscale:

```bash
sudo tailscale up
```

Open the displayed URL and authorize the CM5 in the existing tailnet.

Then pair Remote Desktop Commander once:

```bash
~/.local/bin/desktop-commander remote
```

Authorize the device in the Remote Desktop Commander flow. When it reports that the device is connected, press `Ctrl+C` once. The foreground process is no longer needed after credentials have been saved.

## C2 — persistent services

Run:

```bash
curl -fsSL \
https://raw.githubusercontent.com/autoklinika/ECU_Platrorm_V2/setup/cm5-bootstrap/scripts/finalize_cm5_stage_c_remote.sh \
-o ~/finalize_cm5_stage_c_remote.sh

chmod +x ~/finalize_cm5_stage_c_remote.sh
~/finalize_cm5_stage_c_remote.sh
```

This step:

- configures user `ecu` as Tailscale operator,
- enables `loginctl linger`,
- creates `desktop-commander-remote.service` under `systemd --user`,
- enables and starts the service,
- verifies both remote-access components.

Expected marker:

```text
STAGE_C_REMOTE=PASS
```

## C3 — independent validation

After C2, and again after a future reboot, run:

```bash
~/validate_cm5_stage_c_remote.sh
```

The stage is VERIFIED only when Tailscale and Desktop Commander both survive reboot without a manually open terminal.

## Security boundary

- No GitHub credentials are installed by this stage.
- Remote Desktop Commander credentials remain local device state.
- Tailscale identity/state remains local device state.
- Neither credential set is committed to Git.
- Core must not depend on Tailscale, Node.js, npm or Remote Desktop Commander.


## Pre-reboot physical validation — 2026-10-06

Status: **PASS**

Observed:

- Tailscale: enabled + active
- Tailscale IP: `100.92.219.91`
- Node.js: `v22.23.3`
- npm: `10.9.9`
- Remote Desktop Commander: `0.2.52`
- persistent user service: enabled + active
- `Linger=yes`
- fresh MCP endpoint visible from ChatGPT

Final gate: one reboot must prove that both remote-access paths recover automatically.
