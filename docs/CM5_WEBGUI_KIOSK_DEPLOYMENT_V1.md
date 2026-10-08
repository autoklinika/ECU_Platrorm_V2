# ECU Bench Platform — CM5 secure kiosk installation (V1)

## Status and authority

Deployment of the reviewed V1 WebGUI from branch
webgui/home-v1-i18n-20261008, **without a merge to production main**.

The installer is a root-gated one-time cutover, not a background task.
It changes only the local kiosk, a static HTTP service, a targeted WaveShare
touchscreen permission rule, and the kiosk's own filesystem/profile.
It does **not** alter CAN, Bench Agent, CORE, DUT Profiles or the ECU.

**Important:** A previous Stage D deployment ran the browser as Linux
user ecu, which can access the Bench-agent Unix socket. That deployment
is **not** an acceptable security boundary for a future API-connected GUI.

## Target separation

| Component | OS identity | Scope |
| --- | --- | --- |
| Cage + Chromium kiosk | ecu-kiosk | No operator, sudo, dialout, spi, i2c, gpio or broad input group; only video/render and the WaveShare event device |
| Read-only WebGUI HTTP host | systemd DynamicUser | 127.0.0.1:8877 only, root-owned static files |
| ECU/Bench runtime | Existing identities | Not changed and not contacted by the GUI |
| API | Not implemented/connected | Requires an independent authentication and authorization release gate |

The browser service uses NoNewPrivileges, empty capability sets,
ProtectSystem=strict, a dedicated browser profile and restricted address
families (not AF_CAN/AF_PACKET). It can reach loopback for static content
but systemd IPAddressDeny/Allow restricts network egress. Chromium's
sandbox is **not** disabled.

The static server has no endpoints for diagnostic commands or raw CAN,
returns HTTP 405 for POST and never serves the privileged filesystem.
Only whitelisted, root-owned UI assets can be read.

## One-time installation on the CM5

Prerequisites:
- A working SSH or VS Code terminal on the CM5 with administrative
  permission (enter your sudo password locally; never post credentials).
- The WaveShare touch panel detected as USB 0712:0009.
- The WebGUI development worktree is clean and on branch
  webgui/home-v1-i18n-20261008.
- You can use the external terminal if the touchscreen temporarily
  goes blank during the kiosk user switch.

Run **one** root-gated installer from the CM5 terminal:

    cd /home/ecu/ECU_WebGUI_Home_V1
    sudo bash scripts/install_cm5_webgui_v1.sh

The installer checks the source and host, finds the WaveShare device,
checks that TCP/8877 is unused, and creates a root-only backup under
/var/backups/ecu-platform-kiosk/pre-webgui-v1-*/.

It creates ecu-kiosk with /usr/sbin/nologin and a private profile at
/var/lib/ecu-kiosk; copies only the needed WebGUI assets to a versioned
root-owned directory under /opt/ecu-platform/webgui/releases/; installs
the two systemd service configurations; and permits touchscreen input
**only** for USB ID 0712:0009 through a udev device rule.

The static host is started and checked before the existing kiosk service
is restarted with the new identity. The new kiosk performs a privileged-
resource / raw-CAN isolation preflight inside its own systemd sandbox.
The installer verifies a running Chromium kiosk, touchscreen device read
permission, lack of Bench-agent access, and an HTTP read-only policy.
If the cutover fails, it invokes the automatic rollback.

Successful output contains:

    ECU_WEBGUI_HTTP=PASS
    ECU_KIOSK_SECURITY_PREFLIGHT=PASS
    ECU_WEBGUI_INSTALL=PASS
    ECU_WEBGUI_BENCH_AGENT_ISOLATION=PASS

Do not infer actual touchscreen operation from file permissions alone.
With a working GUI on the physical display, tap the title Ecu Bench Platform
to open/close the side panel, then navigate to SETTINGS -> LANGUAGE and
select Polish / English. Record this manual touch acceptance explicitly.

The service is configured to start automatically at boot. A controlled
reboot and post-reboot screen/touch confirmation are **separate** final
acceptance checks and are not performed by this installer.

## Validation after cutover

    systemctl is-active ecu-kiosk.service
    systemctl is-active ecu-webgui-static.service
    systemctl show ecu-kiosk.service -p User -p Group -p SupplementaryGroups
    id ecu-kiosk
    curl -I http://127.0.0.1:8877/
    sudo journalctl -u ecu-kiosk.service -n 50 --no-pager

Expect User=ecu-kiosk; never User=ecu. The static service must be bound
to 127.0.0.1 only; do not expose this server through Tailscale or a router.

## Recovery / rollback

On installation failure, rollback automatically restores the prior
ecu-kiosk.service and /etc/default/ecu-kiosk. If a console still shows
an error, use the root-owned emergency rollback helper:

    sudo /usr/local/sbin/ecu-webgui-rollback

Rollback also restores the old WaveShare input group and kiosk service,
and stops the static WebGUI service. The unused, non-privileged kiosk account
and read-only asset snapshot are retained for forensic review.

Until the root installer has actually been executed and the validation
outputs reviewed, treat the deployment as **STAGED, NOT INSTALLED**.

## Next phase: API

API work starts only after kiosk isolation and the relevant physical
acceptance gates pass. The authenticated API must be separate from the
browser and from the privileged Bench-agent socket. Initial v1 GUI
capabilities should be read-only, versioned and DUT-neutral (system health,
Bench/DUT snapshot, supported capabilities and eventual diagnostics).
Raw CAN transmit, DTC erasure, ECU reset, security unlock, power outputs
and actuator control are outside this initial API stage.
