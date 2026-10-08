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

## 2026-10-08 installation incident: ECU_WEBGUI_TOUCH_PERMISSIONS=FAIL

Actual host evidence: the initial 91-ecu-kiosk-touch.rules correctly
matched WaveShare 0712:0009 and set GROUP=ecu-kiosk, but Debian/Raspberry
Pi's /usr/lib/udev/rules.d/99-com.rules subsequently set GROUP=input for
all input event devices. The real udevadm test reported both assignments
in that exact order. The installer failed before the kiosk service restart,
but an explicit exit 1 bypassed its former ERR trap. The on-disk kiosk
unit therefore changed while systemd still held the old running User=ecu
service. No isolation gate passed in that failed installation.

### Corrected installation

- The hardware-specific rule uses systemd-udev final assignments:
  GROUP:="ecu-kiosk", MODE:="0660". These prevent the later
  99-com.rules rule from overriding the WaveShare input event group.
  udevadm verify and the unit tests now check the corrected rule.
- An EXIT trap runs also on explicit exit 1, and invokes automatic rollback
  for any failure after the root-owned recovery helper is installed.
- A retry detects the partial previous cutover and calls the existing
  root-owned recovery helper. It verifies the restored legacy unit before
  beginning the fresh, backed-up installation. This avoids falsely
  reporting ALREADY_INSTALLED when only the unit file had changed.
- Do not reboot the device while a failed cutover remains unresolved.
- Manual emergency recovery:
      sudo /usr/local/sbin/ecu-webgui-rollback
- Retry using the corrected, clean development branch:
      cd /home/ecu/ECU_WebGUI_Home_V1
      sudo bash scripts/install_cm5_webgui_v1.sh
- The retry should show ECU_WEBGUI_PARTIAL_CUTOVER=RECOVERED before
  progressing to the WebGUI HTTP/kiosk/security gates. Do not infer
  successful deployment until the full gates, physical touch, and reboot
  acceptance complete.

## 2026-10-08 second installation incident: Cage SIGABRT after service switch

A second installation progressed past the corrected WaveShare udev rule:
- ECU_WEBGUI_PARTIAL_CUTOVER=RECOVERED
- ECU_WEBGUI_HTTP=PASS
- ECU_WEBGUI_CUTOVER=START
- Cage/Chromium immediately failed with exit status 6/ABRT.
- The installer bounded its wait but the service's Restart=always resulted
  in more than ten short-lived launch attempts before automatic rollback.
- ECU_WEBGUI_AUTO_ROLLBACK=START / ECU_WEBGUI_ROLLBACK=PASS.
- Independent CM5 follow-up confirms the original Stage D kiosk runs under
  user ecu, the new static service is inactive, WaveShare event1 is restored
  to root:input, and the development worktree is clean.

### Root cause reproducibly isolated

The new kiosk unit carried **ProtectHome=read-only** together with
ReadWritePaths=/run/user. On this Debian trixie systemd 257 + Cage 0.3.1,
this makes the XDG_RUNTIME_DIR (/run/user/<UID>) effectively read-only in
the service's mount namespace. Cage cannot create its Wayland lock/socket.

An independent, UNPRIVILEGED probe uses WLR_BACKENDS=headless (no actual
DRM/TTY/display touched), retaining all relevant mount restrictions:
- With ProtectHome=read-only and ReadWritePaths=/run/user, Cage logs
  "unable to open lockfile", then "Unable to open Wayland socket:
  Invalid argument"; its wlroots cleanup triggers an assertion in
  wlr_output_layout_destroy, yielding SIGABRT / exit status 6.
- With InaccessiblePaths=/home /root instead and the same
  ReadWritePaths=/run/user, Cage starts and cleanly exits status 0.
- This A/B test was run on the actual CM5 and can be repeated as an
  unprivileged developer using scripts/probe_cm5_wayland_sandbox.sh.

This is **evidence for the crash mechanism**, not yet a complete physical
acceptance of the new kiosk. The headless test does not exercise DRM, HDMI,
logind seat acquisition or touchscreen input as user ecu-kiosk.

### Remediation in kiosk service

- Replace ProtectHome=read-only with **InaccessiblePaths=/home /root**.
  The kiosk home at /var/lib/ecu-kiosk remains accessible for its
  dedicated Chromium profile. Operator home directories remain denied,
  but the Wayland runtime directory remains writable.
- Preserve NoNewPrivileges, zero capabilities, ProtectSystem=strict,
  restricted address families (no AF_CAN/AF_PACKET) and loopback-only
  network policy.
- Add a sandbox preflight that binds a temporary AF_UNIX socket in
  /run/user/<UID> **before Cage launches**; it fails closed if the
  Wayland runtime is not owned/writable by the kiosk user.
- Explicitly route compositor stdout/stderr to the system journal.
- Bound failures to two launch attempts in 30 seconds:
  StartLimitBurst=2; Restart=on-failure (3-second delay).
- Retain automatic rollback if the new kiosk does not stabilize.

After local tests and CI pass, run one controlled installation from the
unchanged development branch:

    cd /home/ecu/ECU_WebGUI_Home_V1
    sudo bash scripts/install_cm5_webgui_v1.sh

The service should now pass its Wayland bind preflight without weakening
browser/Bench isolation. Do NOT merge to production main until physical
touch, restart recovery and the API authorization gates are accepted.

### Additional controlled Cage + Chromium check

A nonphysical test under the corrected systemd mount policy launched
Cage with WLR_BACKENDS=headless and Chromium as its kiosk application.
The process remained running for the configured six-second bounded
timeout; timeout returned 124 as expected. The log showed "Starting
headless backend" and did not show Cage's former SIGABRT or inability
to open its Wayland socket. This is a stronger smoke test of the
corrected process/mount model, but it still **does not prove** physical
DRM output, a PAM session under ecu-kiosk, or the WaveShare touch path.

The physical kiosk installation still needs one separately gated
administrator cutover and actual operator verification.
