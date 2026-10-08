import {
  applyTranslations,
  readLocale,
  storeLocale
} from "./i18n.mjs";

const PAGES = new Set(["home", "tests", "can", "settings", "language"]);
const root = document.getElementById("application");
const sidebarToggle = document.getElementById("sidebar-toggle");
const sidebar = document.getElementById("side-panel");
const sidebarVeil = document.getElementById("sidebar-veil");
const clock = document.getElementById("local-clock");
let browserStorage = null;
try { browserStorage = window.localStorage; } catch { /* restricted kiosk */ }
let currentLocale = readLocale(browserStorage);
let sidebarOpen = false;

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

function renderLocale(locale) {
  currentLocale = locale;
  document.documentElement.lang = locale;
  applyTranslations(root, currentLocale);
  root.querySelectorAll("[data-locale]").forEach((button) => {
    button.setAttribute("aria-pressed", String(button.dataset.locale === currentLocale));
  });
}

sidebarToggle.addEventListener("click", () => setSidebarOpen(!sidebarOpen));
sidebarVeil.addEventListener("click", () => setSidebarOpen(false));

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
renderLocale(currentLocale);
renderRoute();
updateClock();
window.setInterval(updateClock, 1000);
