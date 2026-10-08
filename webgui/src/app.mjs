import {
  applyTranslations, readLocale, storeLocale, translate
} from "./i18n.mjs";
import { ApiSession, ApiError } from "./api-client.mjs";

const PAGES = new Set(["home", "tests", "can", "settings", "language"]);
const root = document.getElementById("application");
const sidebarToggle = document.getElementById("sidebar-toggle");
const sidebar = document.getElementById("side-panel");
const sidebarVeil = document.getElementById("sidebar-veil");
const clock = document.getElementById("local-clock");
const tokenInput = document.getElementById("api-token");
const connectButton = document.getElementById("api-connect");
const disconnectButton = document.getElementById("api-disconnect");
const session = new ApiSession();

let browserStorage = null;
try { browserStorage = window.localStorage; } catch { /* restricted kiosk */ }
let currentLocale = readLocale(browserStorage);
let sidebarOpen = false;
let refreshVersion = 0;
let loginPending = false;
let authMessage = "api.notAuthenticated";
let results = { about: null, interfaces: null, dut: null, readout: null };
let failures = { about: null, interfaces: null, dut: null, readout: null };

function t(key) { return translate(currentLocale, key); }
function text(id, value) { document.getElementById(id).textContent = value; }
function emptyResults() {
  results = { about: null, interfaces: null, dut: null, readout: null };
  failures = { about: null, interfaces: null, dut: null, readout: null };
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
  root.querySelectorAll("[data-page]").forEach((section) => {
    section.hidden = section.dataset.page !== page;
  });
  setSidebarOpen(false);
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
  list.replaceChildren();
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
  text("can-status", t("can.source"));
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

function renderDtc() {
  const details = document.getElementById("dtc-details");
  const rows = document.getElementById("dtc-rows");
  details.hidden = true;
  rows.replaceChildren();
  if (!session.connected) {
    text("dtc-status", t("api.signInRequired"));
    return;
  }
  if (!results.about) {
    text("dtc-status", t("api.dataUnavailable"));
    return;
  }
  if (!results.readout) {
    const code = failures.readout?.code;
    text("dtc-status", code === "readout_expired" ? t("dtc.expired")
      : code === "backend_unavailable" ? t("dtc.missing")
      : t("api.dataUnavailable"));
    return;
  }
  const readout = results.readout;
  text("dtc-status", t("dtc.historical"));
  text("dtc-captured", formatLocalTimestamp(new Date(readout.captured_at_unix_ms)));
  text("dtc-profile", String(readout.profile_id));
  text("dtc-protocol", readout.dtcs.protocol);
  text("dtc-count", String(readout.dtcs.entries.length));
  details.hidden = false;
  if (readout.dtcs.entries.length === 0) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 2;
    cell.textContent = t("dtc.zero");
    row.append(cell);
    rows.append(row);
  }
  for (const item of readout.dtcs.entries) {
    const row = document.createElement("tr");
    const code = document.createElement("td");
    const mask = document.createElement("td");
    code.textContent = item.code;
    mask.textContent = "0x" + item.status_mask.toString(16).padStart(2, "0").toUpperCase();
    row.append(code, mask);
    rows.append(row);
  }
}

function renderApi() {
  const active = session.connected;
  connectButton.disabled = active || loginPending;
  disconnectButton.disabled = !active;
  tokenInput.disabled = active || loginPending;
  text("api-auth-status", t(active ? "api.authenticated" : authMessage));
  const healthy = active && results.about !== null;
  text("connection-value", t(healthy ? "api.apiReady" : "header.unavailable"));
  const can = healthy ? selectedCan() : null;
  text("interface-value", can?.name ?? "—");
  // A configured bitrate is not a claim that a DOWN bus is transmitting.
  text("bitrate-value", can ? bitrate(can.bitrate) + " (" + canState(can) + ")" : "—");
  text("module-value", healthy && results.dut
    ? (results.dut.profile_label || "ID " + results.dut.profile_id) : "—");
  renderCan();
  renderDtc();
}

function signOut(message = "api.notAuthenticated") {
  ++refreshVersion;
  session.signOut();
  tokenInput.value = "";
  authMessage = message;
  emptyResults();
  renderApi();
}

async function connect() {
  if (loginPending || session.connected) return;
  ++refreshVersion;
  const credential = tokenInput.value;
  tokenInput.value = "";
  loginPending = true;
  authMessage = "api.connecting";
  renderApi();
  try {
    await session.signIn(credential);
    authMessage = "api.authenticated";
    renderApi();
    await refresh();
  } catch (error) {
    if (!session.connected) {
      authMessage = error instanceof ApiError && error.code === "invalid_token_format"
        ? "api.invalidToken" : "api.connectionFailed";
    }
    renderApi();
  } finally {
    loginPending = false;
    renderApi();
  }
}

async function refresh() {
  if (!session.connected) {
    if (results.about !== null) signOut("api.sessionExpired");
    else renderApi();
    return;
  }
  const version = ++refreshVersion;
  const paths = [
    [ "about", "/api/v1/about" ],
    [ "interfaces", "/api/v1/interfaces" ],
    [ "dut", "/api/v1/dut" ],
    [ "readout", "/api/v1/readouts/dtc/latest" ]
  ];
  const responses = await Promise.allSettled(paths.map(([, path]) => session.read(path)));
  if (version !== refreshVersion) return;
  if (!session.connected) {
    signOut("api.sessionExpired");
    return;
  }
  const nextResults = {};
  const nextFailures = {};
  responses.forEach((response, index) => {
    const name = paths[index][0];
    nextResults[name] = response.status === "fulfilled" ? response.value : null;
    nextFailures[name] = response.status === "rejected" ? response.reason : null;
  });
  // When the API is unreachable, clear *all* displayed values, including
  // historical data; a prior successful response must never masquerade as live.
  if (!nextResults.about) {
    emptyResults();
    failures.about = nextFailures.about;
  } else {
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
  renderApi();
}

sidebarToggle.addEventListener("click", () => setSidebarOpen(!sidebarOpen));
sidebarVeil.addEventListener("click", () => setSidebarOpen(false));
connectButton.addEventListener("click", connect);
disconnectButton.addEventListener("click", () => signOut());
tokenInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter") { event.preventDefault(); connect(); }
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
    window.location.hash = "/" + routeChoice.dataset.route;
    renderRoute();
  }
});

document.addEventListener("keydown", (event) => {
  if (event.key === "Escape" && sidebarOpen) {
    setSidebarOpen(false);
    sidebarToggle.focus();
  }
});
window.addEventListener("hashchange", renderRoute);
window.addEventListener("pagehide", () => signOut());
renderLocale(currentLocale);
renderRoute();
updateClock();
window.setInterval(() => {
  updateClock();
  if (session.connected) void refresh();
  else if (authMessage === "api.authenticated") signOut("api.sessionExpired");
}, 10000);
