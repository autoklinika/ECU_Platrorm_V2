import {
  applyTranslations, readLocale, storeLocale, translate
} from "./i18n.mjs";
import { KioskSession, ApiError } from "./api-client.mjs";
import { SacConnectionFlow } from "./sac-connect-flow.mjs";
import { SacParameterMonitor } from "./sac-parameter-monitor.mjs";

const PAGES = new Set([
  "home", "tests", "test-truck", "test-agri", "test-ohv",
  "truck-man", "truck-daf", "truck-scania", "truck-iveco", "truck-mb",
  "sac-connecting", "sac-identification", "sac-communication-error",
  "daf-sac", "sac-dtc", "sac-activations", "sac-programming",
  "can", "settings", "language"
]);
const SAC_PAGES = new Set(["daf-sac", "sac-dtc", "sac-activations", "sac-programming"]);
const SAC_DTC_PROFILES = new Set([0xDAF00025, 0xDAF00050]);
const root = document.getElementById("application");
const sidebarToggle = document.getElementById("sidebar-toggle");
const sidebar = document.getElementById("side-panel");
const sacNavigation = document.getElementById("sac-side-navigation");
const detailOverlay = document.getElementById("dtc-detail-overlay");
const detailClose = document.getElementById("dtc-detail-close");
const dtcRows = document.getElementById("dtc-rows");
const sidebarVeil = document.getElementById("sidebar-veil");
const clock = document.getElementById("local-clock");
const session = new KioskSession();
const sacFlow = new SacConnectionFlow(renderSacConnectionPhase);
const SAC_FLOW_PAGES = new Set(["sac-connecting", "sac-identification",
                                 "sac-communication-error"]);


let browserStorage = null;
try { browserStorage = window.localStorage; } catch { /* restricted kiosk */ }
let currentLocale = readLocale(browserStorage);
let sidebarOpen = false;
let displayedPage = null;
let refreshVersion = 0;
let results = { about: null, interfaces: null, dut: null, readout: null, parameters: null };
let failures = { about: null, interfaces: null, dut: null, readout: null, parameters: null };
let monitorStatus = "idle";
let parameterOperation = null;
let sacConnectedAt = 0;
let lastDtcStamp = null;
const parameterMonitor = new SacParameterMonitor({
  read: async () => {
    // Only FE96 and passive FEAE: the privileged service already knows the
    // bitrate/profile from the ONE initial VIN/HW/SW connection.
    const operation = await session.readSacParameters();
    const parameters = operation.parameters_published
      ? await session.read("/api/v1/readouts/daf-sac/parameters/latest")
      : null;
    return {operation, parameters};
  },
  onUpdate: (event) => {
    if (routeFromHash() !== "daf-sac" || document.hidden ||
        sacFlow.phase !== "accepted") return;
    monitorStatus = event.status;
    if (event.status === "updated") {
      parameterOperation = event.operation;
      results.parameters = event.parameters;
      failures.parameters = null;
    } else if (event.status === "busy") {
      // A previous screen's native operation may still be draining;
      // do not clear last valid values or claim communication failure.
      renderSacParameters();
      return;
    } else if (event.status === "unavailable" || event.status === "timeout") {
      parameterOperation = event.operation;
      results.parameters = null;
      failures.parameters = null;
    } else if (event.status !== "reading") {
      // Transport failure, ECU change or invalid snapshot: fail closed.
      results.parameters = null;
      failures.parameters = event.error ?? new ApiError(event.status);
    }
    renderSacParameters();
  }
});

function t(key) { return translate(currentLocale, key); }
function text(id, value) {
  const output = document.getElementById(id);
  const next = String(value);
  if (output.textContent !== next) output.textContent = next;
}
function emptyResults() {
  results = { about: null, interfaces: null, dut: null, readout: null, parameters: null };
  failures = { about: null, interfaces: null, dut: null, readout: null, parameters: null };
}

function setSidebarOpen(open) {
  sidebarOpen = open;
  root.classList.toggle("sidebar-open", open);
  sidebarToggle.setAttribute("aria-expanded", String(open));
  sidebar.setAttribute("aria-hidden", String(!open));
  sidebar.inert = !open;
}

function routeFromHash() {
  const page = window.location.hash.startsWith("#/")
    ? window.location.hash.substring(2)
    : "home";
  return PAGES.has(page) ? page : "home";
}

function renderRoute() {
  const page = routeFromHash();
  if (SAC_PAGES.has(page) && sacFlow.phase !== "accepted") {
    window.location.hash = "/truck-daf";
    return;
  }
  if (SAC_FLOW_PAGES.has(page) &&
      !((page === "sac-connecting" && sacFlow.phase === "connecting") ||
        (page === "sac-identification" && sacFlow.phase === "identified") ||
        (page === "sac-communication-error" && sacFlow.phase === "failed"))) {
    window.location.hash = "/truck-daf";
    return;
  }
  // Duplicate hashchange/tap events should not repaint the same workspace.
  if (displayedPage === page) {
    // Clicking the current item in the drawer must still close the veil.
    setSidebarOpen(false);
    return;
  }
  displayedPage = page;
  root.querySelectorAll("[data-page]").forEach((section) => {
    section.hidden = section.dataset.page !== page;
  });
  // SAC tools are context-specific: never visible for another DUT or catalog.
  sacNavigation.hidden = !SAC_PAGES.has(page);
  sidebarToggle.textContent = SAC_PAGES.has(page) ? t("sac.menu") : "Ecu Bench Platform";
  sidebarToggle.disabled = !SAC_PAGES.has(page);
  if (page !== "sac-dtc") closeDtcDetails();
  sacNavigation.querySelectorAll("button[data-route]").forEach((button) => {
    const active = button.dataset.route === page;
    button.setAttribute("aria-current", active ? "page" : "false");
  });
  setSidebarOpen(false);
  document.getElementById("main-content").scrollTop = 0;
  syncParameterMonitoring();
  renderApi();
}

function syncParameterMonitoring() {
  const enabled = routeFromHash() === "daf-sac" &&
    sacFlow.phase === "accepted" && !!sacFlow.identity &&
    !["session_expired","profile_mismatch"].includes(monitorStatus) &&
    !document.hidden;
  if (!enabled) {
    parameterMonitor.stop();
    // Terminal session/profile errors survive page changes; only an explicit
    // fresh connection resets them.
    if (monitorStatus !== "session_expired" &&
        monitorStatus !== "profile_mismatch") monitorStatus = "idle";
    return;
  }
  if (!parameterMonitor.active) {
    // No archived values become current when returning to this screen.
    results.parameters = null;
    parameterOperation = null;
    failures.parameters = null;
    monitorStatus = "reading";
    parameterMonitor.start(sacFlow.identity);
  }
}

function renderSacConnectionPhase(phase) {
  if (phase === "connecting") {
    window.location.hash = "/sac-connecting";
  } else if (phase === "identified") {
    text("sac-identity-vin", sacFlow.identity.vin ?? t("sac.vinUnprogrammed"));
    text("sac-identity-sw", sacFlow.identity.software);
    text("sac-identity-hw", sacFlow.identity.hardware);
    text("sac-identity-bitrate", String(sacFlow.identity.bitrate / 1000) + " kbit/s");
    window.location.hash = "/sac-identification";
  } else if (phase === "failed") {
    const error = sacFlow.failure;
    const code = error instanceof ApiError ? error.code : "network_unavailable";
    const key = code === "session_expired" || code === "unauthorized"
      ? "sac.authenticationRequired"
      : code === "bench_busy" ? "sac.benchBusy"
      : code === "network_unavailable" || code === "not_found"
      ? "sac.serviceUnavailable" : "sac.checkConnection";
    text("sac-communication-error-message", t(key));
    window.location.hash = "/sac-communication-error";
  } else if (phase === "accepted") {
    sacConnectedAt = Date.now();
    lastDtcStamp = null;
    monitorStatus = "idle";
    window.location.hash = "/daf-sac";
    void refresh();
  }
  // hashchange owns route transitions. Do not synchronously repeat the
  // navigation/render from the SAC phase callback.
}

let activeConnection = null;
function startSacConnection() {
  if (sacFlow.phase === "connecting") return;
  parameterMonitor.stop();
  void sacFlow.begin(async isCurrent => {
    // The two native operations are not cancelable server-side. A cancelled
    // connection or previous parameter probe must finish before re-entry.
    await parameterMonitor.whenIdle();
    if (activeConnection) await activeConnection.catch(() => {});
    if (!isCurrent()) throw new ApiError("session_changed");
    // BUSY means the isolated adapter is completing an earlier native
    // operation. Brief bounded retry without changing the selected DUT.
    for (let attempt = 0; attempt < 6; attempt++) {
      if (!isCurrent()) throw new ApiError("session_changed");
      const request = session.identifySac();
      activeConnection = request;
      try { return await request; }
      catch (error) {
        if (!(error instanceof ApiError) || error.code !== "bench_busy" ||
            attempt === 5) throw error;
        await new Promise(resolve => window.setTimeout(resolve, 500));
      } finally {
        if (activeConnection === request) activeConnection = null;
      }
    }
  });
}

function formatLocalTimestamp(date) {
  const two = (number) => String(number).padStart(2, "0");
  return String(date.getFullYear()) + "-"
    + two(date.getMonth() + 1) + "-"
    + two(date.getDate()) + " "
    + two(date.getHours()) + ":"
    + two(date.getMinutes()) + ":"
    + two(date.getSeconds());
}

function updateClock() {
  const now = new Date();
  clock.textContent = formatLocalTimestamp(now);
  clock.dateTime = now.toISOString();
  if (routeFromHash() === "daf-sac" && sacFlow.phase === "accepted")
    renderSacParameters();
}

function selectedCan() {
  if (!results.interfaces) return null;
  return results.interfaces.interfaces.find((item) => item.name === "can0")
    ?? results.interfaces.interfaces[0] ?? null;
}

function canState(item) {
  if (item.bus_off) return t("can.busOff");
  return item.up ? t("can.up") : t("can.down");
}

function bitrate(value) {
  return value > 0 ? String(value / 1000) + " kbit/s" : "—";
}

function renderCan() {
  const list = document.getElementById("can-list");
  const status = document.getElementById("can-status");
  list.replaceChildren();
  status.hidden = false;
  if (!session.connected) {
    text("can-status", t("api.signInRequired"));
    return;
  }
  if (!results.about || !results.interfaces) {
    text("can-status", t("api.dataUnavailable"));
    return;
  }
  if (results.interfaces.interfaces.length === 0) {
    text("can-status", t("can.none"));
    return;
  }
  // The explanatory caption is already present in the static markup.
  // The live status paragraph is reserved for missing/error conditions.
  status.hidden = true;
  status.textContent = "";
  for (const item of results.interfaces.interfaces) {
    const panel = document.createElement("section");
    panel.className = "data-panel";
    const heading = document.createElement("h2");
    heading.textContent = item.name + " — " + canState(item);
    const description = document.createElement("p");
    description.textContent =
      t("can.nominal") + " " + bitrate(item.bitrate) + " · " +
      t("can.dataBitrate") + " " + bitrate(item.data_bitrate) + " · " +
      t("can.fd") + " " + (item.fd_enabled ? t("can.on") : t("can.off")) + " · " +
      t("can.listenOnly") + " " + (item.listen_only ? t("can.on") : t("can.off"));
    panel.append(heading, description);
    list.append(panel);
  }
}

function closeDtcDetails() {
  detailOverlay.hidden = true;
  document.getElementById("dtc-detail-text").textContent = "";
}

function openDtcDetails(code, status) {
  if (routeFromHash() !== "sac-dtc" || !session.connected ||
      !results.readout || !SAC_DTC_PROFILES.has(results.readout.profile_id) ||
      !results.readout.dtcs.entries.some((item) =>
        item.code === code && item.status_mask === status)) return;
  const panel = document.getElementById("dtc-detail-text");
  panel.replaceChildren();
  const heading = document.createElement("p");
  heading.textContent = "DTC " + code + " · " + t("dtc.status") +
    " 0x" + status.toString(16).padStart(2, "0").toUpperCase();
  const explanation = document.createElement("p");
  explanation.textContent = t("dtc.descriptionUnavailable");
  panel.append(heading, explanation);
  detailOverlay.hidden = false;
  detailClose.focus();
}

function clearSacParameterValues() {
  for (const name of ["sac-permanent-voltage", "sac-ignition-voltage",
                      "sac-pressure-1", "sac-pressure-2"]) {
    text(name, "—");
    document.getElementById(name).nextElementSibling.hidden = true;
  }
}

function renderSacParameters() {
  // An identification result gates SAC access. Never pass off another
  // module's archived measurements as a result of this connection.
  // Never reuse stale measurements after auth loss, backend failure or
  // another DUT selection. Each record is historical, not live telemetry.

  if (sacFlow.phase !== "accepted" || !sacFlow.identity) {
    text("sac-parameters-status", t("sac.requiresIdentification"));
    clearSacParameterValues();
    return;
  }
  if (monitorStatus === "profile_mismatch") {
    text("sac-parameters-status", t("sac.dutChanged"));
    clearSacParameterValues();
    return;
  }
  if (monitorStatus === "session_expired") {
    text("sac-parameters-status", t("sac.sessionExpired"));
    clearSacParameterValues();
    return;
  }
  if (monitorStatus === "error" || monitorStatus === "invalid_readout") {
    text("sac-parameters-status", t("sac.monitorCommunicationLost"));
    clearSacParameterValues();
    return;
  }
  if (monitorStatus === "reading" && !results.parameters) {
    text("sac-parameters-status", t("sac.monitorReading"));
    clearSacParameterValues();
    return;
  }
  if (!parameterOperation?.parameters_published) {
    const reason = parameterOperation?.parameters_status;
    text("sac-parameters-status", t(
      reason === "timeout" ? "sac.parameterTimeout"
        : reason === "invalid" ? "sac.parameterInvalid"
        : "sac.noReadout"));
    clearSacParameterValues();
    return;
  }
  if (!session.connected) {
    text("sac-parameters-status", t("api.signInRequired"));
    clearSacParameterValues();
    return;
  }
  if (!results.about) {
    text("sac-parameters-status", t("api.dataUnavailable"));
    clearSacParameterValues();
    return;
  }
  const record = results.parameters;
  if (record && (record.profile_id !== sacFlow.identity.profile_id ||
      record.profile_id !== parameterOperation?.profile_id ||
      record.captured_at_unix_ms !== parameterOperation.parameter_captured_at_unix_ms ||
      record.completed_generation !== parameterOperation.parameter_completed_generation)) {
    text("sac-parameters-status", t("sac.profileMismatch"));
    clearSacParameterValues();
    return;
  }
  if (!record || !SAC_DTC_PROFILES.has(record.profile_id)) {
    const reason = failures.parameters?.code;
    text("sac-parameters-status",
      reason === "readout_expired" ? t("sac.readoutExpired")
        : reason === "backend_unavailable" ? t("sac.noReadout")
        : reason === "not_found" ? t("sac.apiUpgradeRequired")
        : record ? t("sac.profileMismatch")
        : t("sac.noReadout"));
    clearSacParameterValues();
    return;
  }
  const ageMs = Date.now() - record.captured_at_unix_ms;
  // A completed result is never a live stream. After 30 seconds retain
  // metadata only: no archived voltage/pressure in the current value cells.
  if (ageMs >= (parameterMonitor.active ? 6000 : 30000) || ageMs < -2000) {
    text("sac-parameters-status", t("sac.archivedCapture") + " " +
      formatLocalTimestamp(new Date(record.captured_at_unix_ms)) +
      " — " + t("sac.refreshRequired"));
    clearSacParameterValues();
    return;
  }
  text("sac-parameters-status", t("sac.historicalCapture") + " " +
    formatLocalTimestamp(new Date(record.captured_at_unix_ms)) +
    " — " + (parameterMonitor.active ? t("sac.monitorActive") : t("sac.notLive")));
  const values = record.parameters;
  text("sac-permanent-voltage", values.permanent_voltage_v.toFixed(1));
  text("sac-ignition-voltage", values.ignition_voltage_v.toFixed(1));
  document.getElementById("sac-permanent-voltage").nextElementSibling.hidden = false;
  document.getElementById("sac-ignition-voltage").nextElementSibling.hidden = false;
  document.getElementById("sac-pressure-1").nextElementSibling.hidden =
    values.pressure1_bar === null;
  document.getElementById("sac-pressure-2").nextElementSibling.hidden =
    values.pressure2_bar === null;
  text("sac-pressure-1", values.pressure1_bar === null
    ? t(values.pgn_feae_observed ? "sac.pressureUnavailable" : "sac.pressureNoFrame")
    : values.pressure1_bar.toFixed(2));
  text("sac-pressure-2", values.pressure2_bar === null
    ? t(values.pgn_feae_observed ? "sac.pressureUnavailable" : "sac.pressureNoFrame")
    : values.pressure2_bar.toFixed(2));
}

function renderDtc() {
  const details = document.getElementById("dtc-details");
  const rows = document.getElementById("dtc-rows");
  const status = document.getElementById("dtc-status");
  if (!session.connected) {
    details.hidden = true;
    status.hidden = false;
    text("dtc-status", t("api.signInRequired"));
    return;
  }
  if (!results.about) {
    details.hidden = true;
    status.hidden = false;
    text("dtc-status", t("api.dataUnavailable"));
    return;
  }
  if (!results.readout) {
    details.hidden = true;
    status.hidden = false;
    const code = failures.readout?.code;
    text("dtc-status", code === "readout_expired" ? t("dtc.expired")
      : code === "backend_unavailable" ? t("dtc.missing")
      : t("api.dataUnavailable"));
    return;
  }
  const readout = results.readout;
  // A latest readout may refer to any ECU. Never attribute it to DAF SAC
  // without the validated SAC profile ID and a UDS-origin readout.
  if (!SAC_DTC_PROFILES.has(readout.profile_id) ||
      readout.dtcs.protocol.toLowerCase() !== "uds" ||
      !sacFlow.identity || readout.profile_id !== sacFlow.identity.profile_id ||
      readout.captured_at_unix_ms < sacConnectedAt) {
    details.hidden = true;
    status.hidden = false;
    text("dtc-status", t("sac.noSessionDtc"));
    return;
  }
  const stamp = readout.captured_at_unix_ms + ":" + readout.completed_generation;
  if (stamp === lastDtcStamp && !details.hidden) return;
  lastDtcStamp = stamp;
  rows.replaceChildren();
  // The static DTC section already labels this as historical. Status text
  // stays reserved for missing/expired/error conditions, never duplicated.
  status.hidden = true;
  status.textContent = "";
  text("dtc-captured", formatLocalTimestamp(new Date(readout.captured_at_unix_ms)));
  text("dtc-profile", String(readout.profile_id));
  text("dtc-protocol", readout.dtcs.protocol);
  text("dtc-count", String(readout.dtcs.entries.length));
  details.hidden = false;
  if (readout.dtcs.entries.length === 0) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 3;
    cell.textContent = t("dtc.zero");
    row.append(cell);
    rows.append(row);
  }
  for (const item of readout.dtcs.entries) {
    const row = document.createElement("tr");
    const code = document.createElement("td");
    const description = document.createElement("td");
    const mask = document.createElement("td");
    const detailButton = document.createElement("button");
    detailButton.type = "button";
    detailButton.className = "dtc-detail-open";
    detailButton.textContent = item.code;
    detailButton.dataset.dtcCode = item.code;
    detailButton.dataset.dtcStatus = String(item.status_mask);
    detailButton.setAttribute("aria-label", "DTC " + item.code +
      " — " + t("dtc.details"));
    code.append(detailButton);
    description.textContent = t("dtc.descriptionUnavailable");
    mask.textContent = "0x" + item.status_mask.toString(16).padStart(2, "0").toUpperCase();
    row.append(code, description, mask);
    rows.append(row);
  }
}

function renderApi() {
  // The same-origin service authenticates internally; no operator login UI.
  const healthy = results.about !== null;
  text("connection-value", t(healthy ? "api.apiReady" : "header.unavailable"));
  text("prototype-api-state", t(healthy ? "api.kioskReady" : "api.kioskOffline"));
  const can = healthy ? selectedCan() : null;
  text("interface-value", can?.name ?? "—");
  // CAN UP/DOWN is a transient transport implementation detail during SAC
  // polling, not a meaningful DUT status indicator for the operator.
  text("bitrate-value", sacFlow.phase === "accepted" && sacFlow.identity
    ? bitrate(sacFlow.identity.bitrate)
    : can ? bitrate(can.bitrate) + " (" + canState(can) + ")" : "—");
  text("module-value", healthy && results.dut
    ? (results.dut.profile_label || "ID " + results.dut.profile_id) : "—");
  if (routeFromHash() === "can") renderCan();
  if (routeFromHash() === "sac-dtc") renderDtc();
  if (routeFromHash() === "daf-sac") renderSacParameters();
}

function clearGuiSession() {
  closeDtcDetails();
  sacFlow.reset();
  ++refreshVersion;
  parameterMonitor.stop();
  monitorStatus = "idle";
  parameterOperation = null;
  sacConnectedAt = 0;
  lastDtcStamp = null;
  emptyResults();
  renderApi();
}

async function refresh() {
  const version = ++refreshVersion;
  const paths = [
    [ "about", "/api/v1/about" ],
    [ "interfaces", "/api/v1/interfaces" ],
    [ "dut", "/api/v1/dut" ]
  ];
  // Screen responsibility: only the DTC page queries DTC readout.
  // Parameters are fetched by SacParameterMonitor ONLY on its own page.
  if (routeFromHash() === "sac-dtc")
    paths.push(["readout", "/api/v1/readouts/dtc/latest"]);
  const responses = await Promise.allSettled(paths.map(([, path]) => session.read(path)));
  if (version !== refreshVersion) return;
  const nextResults = {};
  const nextFailures = {};
  responses.forEach((response, index) => {
    const name = paths[index][0];
    nextResults[name] = response.status === "fulfilled" ? response.value : null;
    nextFailures[name] = response.status === "rejected" ? response.reason : null;
  });
  // Explicitly absent fields are not retained from a previous screen.
  for (const name of ["readout", "parameters"]) {
    nextResults[name] ??= null;
    nextFailures[name] ??= null;
  }
  // When the API is unreachable, clear *all* displayed values, including
  // historical data; a prior successful response must never masquerade as live.
  if (!nextResults.about) {
    emptyResults();
    failures.about = nextFailures.about;
  } else {
    // The screen-scoped polling cycle owns the parameters when active.
    // Global status refresh must not overwrite an in-flight newer capture.
    if (parameterMonitor.active) {
      nextResults.parameters = results.parameters;
      nextFailures.parameters = failures.parameters;
    }
    results = nextResults;
    failures = nextFailures;
  }
  renderApi();
}

function renderLocale(locale) {
  currentLocale = locale;
  document.documentElement.lang = locale;
  applyTranslations(root, currentLocale);
  root.querySelectorAll("[data-locale]").forEach((button) => {
    button.setAttribute("aria-pressed", String(button.dataset.locale === currentLocale));
  });
  displayedPage = null;
  renderRoute();
}

sidebarToggle.addEventListener("click", () => setSidebarOpen(!sidebarOpen));
sidebarVeil.addEventListener("click", () => setSidebarOpen(false));
detailClose.addEventListener("click", closeDtcDetails);
document.getElementById("sac-identity-ok").addEventListener("click", () => {
  sacFlow.accept();
});
document.getElementById("sac-retry").addEventListener("click", startSacConnection);
document.getElementById("sac-cancel").addEventListener("click", () => {
  // Cancel only the UI flow. Let the existing native read perform CAN cleanup.
  sacFlow.reset();
  navigate("truck-daf");
});

detailOverlay.addEventListener("click", (event) => {
  if (event.target === detailOverlay) closeDtcDetails();
});
dtcRows.addEventListener("click", (event) => {
  const target = event.target;
  if (!(target instanceof Element)) return;
  const button = target.closest("button[data-dtc-code]");
  if (!button || !dtcRows.contains(button)) return;
  openDtcDetails(button.dataset.dtcCode, Number(button.dataset.dtcStatus));
});

root.addEventListener("click", (event) => {
  const target = event.target;
  if (!(target instanceof Element)) return;
  const localeChoice = target.closest("button[data-locale]");
  if (localeChoice) {
    renderLocale(storeLocale(browserStorage, localeChoice.dataset.locale));
    return;
  }
  const routeChoice = target.closest("button[data-route]");
  if (routeChoice && PAGES.has(routeChoice.dataset.route)) {
    if (routeChoice.dataset.route === "daf-sac" &&
        routeFromHash() === "truck-daf") {
      startSacConnection();
      return;
    }
    navigate(routeChoice.dataset.route);
  }
});

document.addEventListener("keydown", (event) => {
  if (event.key === "Escape" && !detailOverlay.hidden) {
    closeDtcDetails();
    return;
  }
  if (event.key === "Escape" && sidebarOpen) {
    setSidebarOpen(false);
    sidebarToggle.focus();
  }
});
function navigate(route) {
  if (!PAGES.has(route)) return;
  if (routeFromHash() === route) {
    setSidebarOpen(false);
    return;
  }
  window.location.hash = "/" + route;
}

window.addEventListener("hashchange", () => {
  const page = routeFromHash();
  if (!SAC_PAGES.has(page) && !SAC_FLOW_PAGES.has(page) &&
      sacFlow.phase !== "connecting") sacFlow.reset();
  renderRoute();
  if (page === "sac-dtc") void refresh();
});
document.addEventListener("visibilitychange", () => {
  if (document.hidden) {
    parameterMonitor.stop();
    results.parameters = null;
  }
  syncParameterMonitoring();
  renderSacParameters();
});
window.addEventListener("pagehide", clearGuiSession);
renderLocale(currentLocale);
renderRoute();
updateClock();
void refresh();
// Fast wall clock is independent from backend refresh and hardware I/O.
function scheduleClockTick() {
  const delta = 1000 - (Date.now() % 1000);
  window.setTimeout(() => {
    updateClock();
    scheduleClockTick();
  }, delta);
}
scheduleClockTick();
window.setInterval(() => { void refresh(); }, 10000);
