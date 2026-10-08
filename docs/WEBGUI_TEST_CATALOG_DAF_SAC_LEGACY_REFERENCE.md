# WebGUI V1.2 — TESTS catalog and DAF SAC legacy-QML reference

Status: separate candidate branch webgui/test-catalog-daf-sac-v1.
Stacked on WebGUI API client PR #21. NOT deployed, NOT merged into main.

## Verified legacy reference

Source used for visual / interaction comparison:
GitHub repository autoklinika/ecu_platform, main commit 1db8b3b.
Reference was inspected independently in /tmp, not copied into ECU V2
application or linked to its runtime. Relevant paths:

- src/QML/TestsPage.qml: large, centered 3-column manufacturer tiles,
  220x110 px. The old app had MAN/DAF/SCANIA/IVECO/MB/VOLVO; the new
  catalog follows the user's explicit five TRUCK brands (no VOLVO yet)
  and adds the new TRUCK / AGRI / OHV category layer.
- src/QML/BrandPage.qml: brand heading and a large 340x140 SAC tile.
- src/QML/Theme.qml and StyledButton.qml: light-gray #F3F3F3 background,
  white data panels, blue #59C8FF rounded buttons, darker #2A84C9 border.
- src/QML/SACMenuPage.qml: PARAMETERS, single white panel, four
  alternating-height rows in this exact order:
  Pressure 1, Pressure 2, Battery Permanent, Battery Ignition.
- src/QML/SACTopBar.qml: fixed MENU toggle, bus status and identifiers.
- src/QML/SACDTCPage.qml: DTC / DESCRIPTION / STATUS columns, row details
  dialog. Historical UI used automatic physical DTC read, Refresh and
  Erase. Those operations are prohibited in this V2 browser client.

## Implemented UI route structure

Home -> TESTS -> TRUCK / AGRI / OHV.
TRUCK -> MAN / DAF / SCANIA / IVECO / MB.
DAF -> SAC.
SAC -> four parameter labels and empty measurement values until a
verified API parameter readout is available.
Top-left MENU -> DTC / Activations / Programming, only inside SAC.
DTC -> historical readout from authenticated GET
/api/v1/readouts/dtc/latest, including code, description-placeholder
and original raw status byte. Tapping a code opens a view-only details
dialog. No destructive or diagnostic requests originate from navigation.

The other manufacturer and AGRI/OHV pages are honest placeholders,
not fake supported DUT profiles.

## Runtime rules

- The WebGUI is a client only. This change has no Core V2, Bench, DUT,
  Application API or privileged bench-agent changes.
- Four SAC measurement labels reflect services implemented and physically
  tested in DAF SAC application, but API V1 does NOT expose measured SAC
  pressures/voltages. The GUI displays em dash, not historical 28V
  measurements pretending to be live. Pressure FE/FF means unavailable,
  not zero bar. Dedicated validated publication is needed next.
- API /about proves API authorization, not physical SAC connection.
- Historical DTC readout is accepted for this module only if profile ID
  matches DAF SAC 250k or 500k and protocol is UDS. Unknown, expired,
  invalid, missing or other-profile readout is not shown as SAC data.
- Legacy DTC descriptions TSV is unverified; new screen explicitly
  marks descriptions as not verified instead of asserting false labels.
- The old direct CockpitController DTC scan/clear, disconnect, resetCAN,
  automatic speed search and other hardware operations are NOT carried
  over. Activations and Programming screens are informational and
  non-operational until separately admitted by an authorized API.
- Token remains private in browser memory only, time-limited. No new
  credentials or storage logic.

## Validation

No physical ECU diagnostic TX is performed. Validations require:
Node syntax, frontend contract/i18n/API-client regression tests,
Python static deployment contract tests, Bash syntax, GitHub WebGUI
Linux/Windows CI and main platform CI on exact candidate head.
An isolated Chromium navigation smoke uses synthetic responses only;
it exercises all selection levels, four unavailable parameters, MENU,
historical DTC table/details, empty activity/programming controls and
absence of GET-before-auth and write requests.

Browser previews are synthetic / unauthenticated design screenshots,
not a physical SAC session or measured values. DTC preview, if present,
uses explicitly synthetic fixture 12AB3F.

## Installation gate (operator-controlled only)

No automatic deployment from this PR. After green CI and user review,
CM5 operator can use an interactive terminal:

  cd ~/ECU_WebGUI_TESTS_SAC_V1
  sudo bash scripts/deploy_cm5_webgui_test_catalog_v1.sh

This installer requires root and a TTY, an exact clean branch,
healthy kiosk/API/bench-agent services, and CAN DOWN. It creates a new
root-owned static release, backs up previous pointer/static-server and
provides scoped automatic rollback. Only kiosk and static-server may
restart; the API, Bench Agent, CAN link and CORE are untouched.

Rollback after an installed candidate:

  sudo /usr/local/sbin/ecu-webgui-test-catalog-rollback

The user must review touchscreen behavior and refresh/reboot separately.
Neither preflight nor browser preview justifies claiming a physical
touchscreen acceptance. Absolutely no merge to main without approval.
