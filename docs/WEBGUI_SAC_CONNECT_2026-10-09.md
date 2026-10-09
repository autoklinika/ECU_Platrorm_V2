# DAF SAC — connect / identification / parameters (9 October 2026)

Branch: `webgui/sac-connect-identify-20261009`. Pending PR and **operator
installation**. No merge to `main`. The previously verified SAC 500 kbit/s
ISO-TP/UDS proof remains the physical reference.

## Operator experience

`TESTS → TRUCK → DAF → SAC` starts an explicit, one-shot read-only
connection request. GUI immediately shows "Komunikacja…" with a 64 px
spinning indicator. A successful UDS read of F190 (VIN), F188 (SW) and F192
(HW) shows the large centered VIN, divider, SW/HW and a single **OK** button
in the layout of the former `autoklinika/ecu_platform` QML
`SACInitPage.qml`, `SACMainPage.qml` and `SACMenuPage.qml`.

For exact `F190 = 17 × FF`, VIN is shown as "NIEZAPROGRAMOWANY", not a
fabricated identifier. Missing/wrong DID, nonpositive UDS identification,
timeout and connection failure show **Błąd komunikacji** with
**Ponów / Wstecz**, never the parameter page. The main SAC parameter page
is available **only after pressing OK** on this successful identity view.
Navigating away or signing out invalidates the browser result.

## Isolation and protocols

- No CAN, `/dev`, privileged Unix socket, shell or sudo access from
  JavaScript; the GUI remains a pure HTTP client with an in-memory session.
- Existing Application API V1 on 127.0.0.1:8878 stays **GET-only**,
  unprivileged and unchanged. No destructive methods are enabled.
- Separate minimal **Linux hardware adapter** bound only to
  127.0.0.1:8879 accepts exactly one operator-triggered
  `POST /api/v1/bench/daf-sac/connect`. Same 64-hex API Bearer credential;
  exact loopback Host and kiosk Origin checks, strict CORS preflight,
  zero-length request, no redirects, one active operation at once, no raw
  CAN frame/config parameter routes and no telemetry streaming.
- The root-owned systemd adapter alone performs *known* CAN Classic
  500 kbit/s setup, runs two **root-installed immutable binaries under
  the unprivileged `ecu` identity**, and unconditionally attempts CAN DOWN
  before any HTTP success. Root process has bounded CAN administration
  and user-switch capabilities, not a generic command execution endpoint.
- First identification `22 F190/F188/F192`, then one read-only completed
  parameters operation `22 FE96` plus observed FEAE. No DTC clear, flash,
  actuation, routine or ECU reset. ACK during passive discovery is **not**
  an acceptance condition; this operator request uses the already validated
  active ISO-TP/UDS read path.
- Parameter results are published through the **existing** secure,
  atomic `sac-parameters-latest.v1` native readout store. The GUI fetches
  them using API V1 and displays them only if the profile and timestamp
  belong to this new connection; null pressure is a dash, not zero.
  Values are shown as a **completed measurement, not live telemetry**.
- No CORE or Bench Runtime changes; the adapter is replaceable for
  other OS/hardware, and the browser code is platform-neutral.

## Preflight and controlled CM5 installation

The API V1.2 physical candidate from `ECU_API_PARAMS_V1` must remain at
`ec1e2c3aa04e` with a clean worktree, previously prepared
`build/params-linux` binaries and a matching release SHA stamp.
Do not install from unreviewed changes. The GUI branch and worktree must
also be exact and clean. The installer rejects an already installed
service and never reconfigures a running `can0`.

Operator command **after PR/CI review**, in interactive VS Code Remote
terminal on the CM5:

```bash
cd ~/ECU_WEBGUI_PARAMS_V1
sudo bash scripts/deploy_cm5_sac_connect_v1.sh
```

This deploys only the isolated SAC HTTP adapter plus the WebGUI static
release, with rollback. It preserves the installed API executable, bearer
token, existing DTC readout, permanent Bench Agent and CAN kernel config.
No physical connection request runs during installation.

Recovery:

```bash
sudo /usr/local/sbin/ecu-sac-connect-rollback
```

After `SAC_CONNECT_DEPLOY=PASS`, connect to API in **Settings** with the
existing authorized token (never committed or placed in screenshots).
Use the kiosk menu `TESTY → TRUCK → DAF → SAC` and inspect the real
identifier result; press OK and verify latest 28 V voltage snapshot or
new actual values. For an error-path check, power down the DUT *before*
starting the test (never unplug CAN while energized), tap SAC and confirm
that "Błąd komunikacji" is shown. Use Wstecz to leave.

## Validation and remaining physical gate

Local validation: 36/36 Node WebGUI tests, 8 Python adapter tests and 4 installer/policy tests,
existing WebGUI deployment tests, shell and JS/Python syntax tests.
The adapter HTTP security suite tests no-token 401, wrong origin,
forbidden path, body rejection, exact CORS, parser invalid replies,
a busy interface and cleanup on simulated failures. GitHub CI runs
Linux/Windows UI checks and a separate Linux-only backend gate.

**The physical tap-to-connect operation is not considered PASS until the
operator installs the new root-owned adapter and performs the kiosk tap.**
Prior operator CLI communication and param publication do not prove the
new GUI-triggered pathway. No hardware read was started automatically.
