# ECU Bench Platform — WebGUI home V1 and localization

Status: **implementation on isolated development branch; not deployed or released**.

## Scope of this increment

The selected visual baseline is the minimal industrial **concept V1**, not
the former Qt/QML architecture. This WebGUI is a static, presentation-only
browser application. No backend, CAN, ECU, power or diagnostic service is
started or contacted.

The home view at the reference kiosk resolution of 1280 × 720 has:

- Original V1 light-gray top bar, renamed **Ecu Bench Platform**.
- Existing top-bar fields retained: connection, interface, bitrate, module,
  local date/time. Connection and measurements show **Unavailable / —** until
  an authenticated, authoritative API reports their actual values. Fake CAN
  status, demo ECU identification and hard-coded DTCs are forbidden.
- Left sidebar initially fully closed. Tap/click the application title to
  toggle it; tap outside or press Escape to close. Sliding transition: 180 ms,
  disabled by the reduced-motion OS preference. Sidebar is intentionally empty
  pending a future user decision.
- Exactly three home tiles: **TESTS**, **CAN**, **SETTINGS**.
- **SETTINGS -> LANGUAGE** offering English and Polish; English is the
  deterministic default (not the browser/OS language). Locale changes update
  visible labels and accessibility attributes without a page reload.

TESTS and CAN are navigation placeholders only. There are no device commands.

## Frontend choice

Dependency-free HTML5 + CSS + standard browser ES modules. No framework,
Node runtime, bundler, CDN or external fonts are needed **on the CM5**.
A local static HTTP server can preview the design; an authenticated API and
deployment server are separate future gates. Browser layout is responsive, with
1280 × 720 and physical touch as the primary design target.

This frontend does **not** link, import, wrap or use the C++ CORE, Bench,
DUT-profile, Linux hardware adapters, or the privileged Bench agent. It does
not use WebUSB/WebSerial, process invocation, local IPC or raw CAN.

## Adding UI languages

1. Create a locale file in webgui/src/locales/, following en.mjs and
   preserving **stable message keys** (for example, home.tests).
2. Register it in webgui/src/i18n.mjs in CATALOGS.
3. Add its selector to webgui/index.html with a BCP 47 language tag.
4. Extend the locale-matrix tests and run Node built-in tests.
5. Check layout length, buttons and accessibility at 1280 × 720.

All user-facing labels and accessibility strings use message keys, except for
the product name and neutral data placeholders. Changing the language is a
**local presentation preference** in localStorage, with key
ecu-bench-webgui.locale.v1. It is not authoritative device/session state.
If storage is unavailable, English is used on reload.

## DTC descriptions and future domain translations

**UI labels and DTC/ECU descriptions are separate catalogs and lifecycles.**
Do not add an OEM DTC explanation to a UI locale file. The ECU Platform
backend / Knowledge data layer must own verified diagnostic content and its
provenance. A future versioned **read-only** API contract should expose a
stable DTC code + status, source identifier, description locale, text and
revision (or validated keyed localized alternatives). The authorized API
chooses what content may be published; the browser never decodes raw CAN or
invents a DTC description.

When a requested language is missing:
- Fallback can use verified original/reference-language text, **with the
  actual language tagged**, instead of pretending it was translated.
- If neither desired nor approved fallback description exists, show no
  description / explicit missing translation; keep the original DTC code.
- Do not machine-translate safety-critical or OEM diagnostic instructions
  inside the browser without an approved content-validation policy.
- UI language is one input to future read-only API language negotiation,
  never an authorization parameter or client-originated DTC.

The module webgui/src/domain-text.mjs provides a small **future-use** selector
for already-authorized language-tagged text. It is not connected to hardware
and does not ship an ECU description database.

## Preview (not kiosk deployment)

On a development machine with Python 3:

    cd webgui
    python3 -m http.server 8765 --bind 127.0.0.1

Then open http://127.0.0.1:8765/. This is a *local developer preview only*,
not an authenticated production server or a way to expose remote clients.

## Release/security gates

This stage must **not** modify /etc/default/ecu-kiosk, restart the kiosk,
expose the GUI over the network, or use the privileged Bench agent.

Per docs/WEBGUI_CLIENT_ONLY_ARCHITECTURE.md, connecting this UI to the
actual kiosk/backend requires a dedicated unprivileged kiosk identity,
hardened browser service, authenticated versioned API with read-only allowlist,
and separate Bench application/authorization controls. CM5 touch, sandbox,
privilege-boundary, API and multi-platform gates must then pass. No merge to
production main without user approval.

## Captured GUI review references

Actual Chromium renders at 1280 × 720 on the CM5 (not AI-generated images):

- [Home — English default](previews/webgui-home-en-1280x720.png)
- [Home — Polish selected](previews/webgui-home-pl-1280x720.png)
- [Settings -> Language](previews/webgui-language-en-1280x720.png)

A read-only Chromium interaction smoke on the CM5 confirmed the default
language, brand-menu toggle, no title overflow, settings navigation,
English-to-Polish transition, local language persistence after refresh, and
return to English. No kiosk service or Bench/DUT runtime was changed.

## Visual refinement — retained V1 information architecture

The V1 layout is retained. Its styling is now a restrained technical
interface: light-gray header and workspace, graphite typography, flat
instrument-like tiles with 1 px borders and a narrow steel-blue top line,
thin section separators, and no decorative icons, gradients, blur, images,
stock illustrations, fabricated diagnostic data, or animated dashboard
elements. The three main tiles remain the only home actions.

The brand itself opens/closes the initially hidden, deliberately empty
side panel. Only that panel uses a short 180 ms transform, suppressed
when the operator requests reduced motion. The language catalog, default
English, the Settings/Language path, and UI/DTC translation separation
are unchanged.

### Actual browser comparisons at 1280x720

Original V1 baseline:
- [English before refinement](previews/webgui-home-en-original-1280x720.png)
- [Polish before refinement](previews/webgui-home-pl-original-1280x720.png)

Refined visual style:
- [English home](previews/webgui-home-en-1280x720.png)
- [Polish home](previews/webgui-home-pl-1280x720.png)
- [Sidebar open, currently intentionally empty](previews/webgui-sidebar-open-1280x720.png)
- [Language selector, English](previews/webgui-language-en-1280x720.png)
- [Language selector, Polish](previews/webgui-language-pl-1280x720.png)

The captures are real Chromium screenshots from the CM5 development
machine, not synthetic mockups. Headless interaction checks confirmed
the toolbar and tiles do not collide; the sidebar can be opened/closed;
English/Polish translation, navigation and language persistence function.
Viewport-geometry checks passed at 1280x720, 1920x1080, 1024x768,
800x600, 600x800, 375x812 and 320x680.

Only WebGUI styling and reference screenshots changed in this refinement.
No live kiosk or DUT services were modified. All production integration
and security gates specified above remain pending.


## Touchscreen cursor visibility

The kiosk now uses a CSS-only rule to hide the mouse pointer across all
WebGUI elements, including buttons and empty areas. Touch and click events
remain enabled; no change to the Cage service, input driver, Chromium flags,
API, Core or Bench Runtime is required. This initially affects regular
desktop browser previews of the same WebGUI too.

On a CM5 already running a root-owned WebGUI release, apply only this CSS
update via scripts/deploy_cursor_css_cm5.sh using a single local sudo
command. The update creates a new versioned static release, atomically
switches the static root and restarts only ecu-kiosk.service, restoring
the prior release if the update fails. Physical pointer visibility and
touch responsiveness require human confirmation after the cutover.
