import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { CATALOGS } from "../src/i18n.mjs";

const read = (path) => readFileSync(new URL("../" + path, import.meta.url), "utf8").replace(/\r\n/g, "\n");
const html = read("index.html");
const app = read("src/app.mjs");
const css = read("styles.css");

test("home page contains exactly three V1-style tiles", () => {
  const home = html.match(/<section class="page" data-page="home">([\s\S]*?)<\/section>/);
  assert.ok(home);
  const tiles = [...home[1].matchAll(/class="tile" data-route="([^"]+)"/g)];
  assert.deepEqual(tiles.map((match) => match[1]), ["tests", "can", "settings"]);
  assert.match(html, /Ecu Bench Platform/);
  assert.match(html, /<html lang="en">/);
});

test("side panel starts closed and is toggled by the brand", () => {
  assert.match(html, /id="sidebar-toggle"[\s\S]*?aria-controls="side-panel" aria-expanded="false"/);
  assert.match(html, /id="side-panel"[\s\S]*?aria-hidden="true" inert/);
  assert.match(app, /sidebarToggle\.addEventListener\("click"/);
  assert.match(app, /sidebar\.inert = !open/);
  assert.match(app, /event\.key === "Escape"/);
  assert.match(css, /transform: translateX\(-102%\)/);
  assert.match(css, /prefers-reduced-motion: reduce/);
});

test("language is selected through Settings -> Language", () => {
  const settings = html.match(/<section class="page" data-page="settings" hidden>([\s\S]*?)<\/section>/);
  const language = html.match(/<section class="page" data-page="language" hidden>([\s\S]*?)<\/section>/);
  assert.ok(settings);
  assert.ok(language);
  assert.match(settings[1], /data-route="language"/);
  assert.match(language[1], /data-locale="en"/);
  assert.match(language[1], /data-locale="pl"/);
  assert.match(app, /document\.documentElement\.lang = locale/);
  assert.match(app, /applyTranslations\(root, currentLocale\)/);
});

test("refined V1 skin stays flat, lightweight and kiosk-friendly", () => {
  assert.match(css, /\.tiles--home\s*\{[^}]*grid-template-columns: repeat\(3, minmax\(0, 1fr\)\)/);
  assert.match(css, /border-top: 3px solid/);
  assert.match(css, /transition: transform 180ms ease, visibility 180ms/);
  assert.match(css, /prefers-reduced-motion: reduce/);
  assert.doesNotMatch(css, /(?:linear|radial|conic)-gradient\(|backdrop-filter/i);
  assert.deepEqual([...css.matchAll(/@keyframes\s+([\w-]+)/g)].map(x => x[1]), ["sac-spin"]);
});

test("all WebGUI elements hide the pointer without disabling touch or click", () => {
  assert.ok(css.includes("body * {\n  cursor: none !important;"));
  assert.match(css, /touch-action: manipulation/);
  assert.ok(app.includes('sidebarToggle.addEventListener("click"'));
  assert.ok(!css.includes("pointer-events: none !important"));
});

test("WebGUI has no diagnostic, hardware or privileged-agent path", () => {
  const frontend = [
    html, css, app, read("src/i18n.mjs"), read("src/domain-text.mjs"),
    read("src/locales/en.mjs"), read("src/locales/pl.mjs")
  ].join("\n");
  const forbidden = [
    /\bfetch\s*\(/,
    /\bWebSocket\s*\(/,
    /XMLHttpRequest/,
    /EventSource/,
    /\/run\/ecu-platform/,
    /(?:sudo|systemctl|child_process|process\.exec)\b/,
    /\b(navigator\.serial|navigator\.usb)\b/,
    /\b(document\.write|eval\s*\()\b/
  ];
  for (const rule of forbidden) {
    assert.doesNotMatch(frontend, rule);
  }
  assert.match(html, /Content-Security-Policy/);
  assert.doesNotMatch(html, /<script[^>]+src="https?:/);
  assert.match(html, /id="interface-value">—/);
  assert.match(html, /id="bitrate-value">—/);
});

test("CAN view does not duplicate its static explanation after a successful API read", () => {
  const canSection = html.match(/<section class="page" data-page="can" hidden>([\s\S]*?)<\/section>/);
  assert.ok(canSection);
  assert.match(canSection[1], /data-i18n="can.source"/);
  assert.match(canSection[1], /id="can-status"/);
  assert.match(app, /const status = document\.getElementById\("can-status"\)/);
  assert.match(app, /status\.hidden = false/);
  assert.match(app, /status\.hidden = true/);
  assert.doesNotMatch(app, /text\("can-status", t\("can.source"\)\)/);
});

test("API client can only read authorized V1 routes without privileged I/O", () => {
  const client = read("src/api-client.mjs");
  assert.match(client, /http:\/\/127\.0\.0\.1:8878/);
  assert.match(client, /credentials: "omit"/);
  assert.match(client, /cache: "no-store"/);
  assert.match(client, /redirect: "error"/);
  assert.match(client, /method: "GET"/);
  assert.match(client, /SESSION_LIFETIME_MS/);
  assert.doesNotMatch(client, /(?:localStorage|sessionStorage|indexedDB|document\.cookie)/);
  assert.doesNotMatch(client, /(?:WebSocket|EventSource|navigator\.serial|navigator\.usb)/);
  assert.doesNotMatch(client, /(?:\/dev\/|\/run\/ecu-platform|sudo|child_process)/);
  assert.doesNotMatch(html, /id="api-token"/);
  assert.match(html, /id="prototype-api-state"/);
  assert.match(html, /connect-src 'self';/);
  assert.doesNotMatch(html, /connect-src[^;]*http:/);
  assert.match(html, /id="dtc-status"/);
  assert.match(html, /id="can-status"/);
  assert.doesNotMatch(html, /value="[0-9a-f]{64}"/);
});

test("there are no hard-coded source-language-only labels in alternate views", () => {
  const keys = [...html.matchAll(/data-i18n="([^"]+)"/g)].map((match) => match[1]);
  const ariaKeys = [...html.matchAll(/data-i18n-aria-label="([^"]+)"/g)]
    .map((match) => match[1]);
  const allKeys = [...keys, ...ariaKeys];
  assert.ok(allKeys.length > 14);
  for (const key of allKeys) {
    assert.match(key, /^[a-z]+\.[A-Za-z0-9]+$/);
    assert.ok(Object.hasOwn(CATALOGS.en, key), "Missing English UI key: " + key);
  }
});


test("legacy-inspired TESTS / TRUCK / DAF / SAC hierarchy and independent placeholders", () => {
  const section = (name) => {
    const match = html.match(new RegExp('<section class="[^"]*" data-page="' + name +
      '" hidden>([\\s\\S]*?)<\\/section>'));
    assert.ok(match, "missing route " + name);
    return match[1];
  };
  for (const page of ["tests", "test-truck", "test-agri", "test-ohv",
    "truck-man", "truck-daf", "truck-scania", "truck-iveco", "truck-mb",
    "daf-sac", "sac-dtc", "sac-activations", "sac-programming"]) section(page);
  for (const [from, routes] of [
    ["tests", ["test-truck", "test-agri", "test-ohv"]],
    ["test-truck", ["truck-man", "truck-daf", "truck-scania", "truck-iveco", "truck-mb"]],
    ["truck-daf", ["daf-sac"]]
  ]) {
    const found = [...section(from).matchAll(/class="tile" data-route="([^"]+)"/g)]
      .map((entry) => entry[1]);
    assert.deepEqual(found, routes, from);
  }
  for (const page of ["test-agri", "test-ohv", "truck-man", "truck-scania",
    "truck-iveco", "truck-mb"]) {
    assert.match(section(page), /catalog.noModules/);
  }
});

test("SAC parameters follow legacy four-row presentation and stay fail-closed", () => {
  const match = html.match(/<section class="page catalog-page catalog-page--sac" data-page="daf-sac" hidden>([\s\S]*?)<\/section>/);
  assert.ok(match);
  const parameterIds = [...match[1].matchAll(/<output id="([^"]+)">—<\/output>/g)]
    .map((entry) => entry[1]);
  assert.deepEqual(parameterIds, [
    "sac-pressure-1", "sac-pressure-2", "sac-permanent-voltage", "sac-ignition-voltage"
  ]);
  assert.match(match[1], /sac-parameter-panel/);
  assert.match(match[1], /id="sac-parameters-status"/);
  assert.doesNotMatch(match[1], /data-i18n="sac.noReadout"/);
  assert.match(app, /SAC_DTC_PROFILES/);
  assert.match(app, /readout\.dtcs\.protocol\.toLowerCase\(\) !== "uds"/);
  assert.doesNotMatch(app, /read_dtcs\(|startDTCRead|clearDTC|CockpitController|SystemController/);
  assert.doesNotMatch(html, /\bERASE\b|\bFLASH\b|\bSTART\b/);
});

test("SAC contextual navigation and DTC details are read-only", () => {
  const nav = html.match(/<nav id="sac-side-navigation"[\s\S]*?<\/nav>/);
  assert.ok(nav);
  for (const name of ["sac-dtc", "sac-activations", "sac-programming"]) {
    assert.ok(nav[0].includes('data-route="' + name + '"'));
  }
  assert.match(app, /sacNavigation\.hidden = !SAC_PAGES\.has\(page\)/);
  assert.match(app, /detailOverlay\.hidden = true/);
  assert.match(app, /detailButton\.dataset\.dtcCode/);
  assert.match(app, /document\.createElement\("button"\)/);
  assert.match(html, /id="dtc-detail-overlay" hidden/);
  assert.match(html, /dtc.description/);
  for (const page of ["sac-activations", "sac-programming"]) {
    assert.match(html, new RegExp('data-page="' + page + '"[\\s\\S]*?sac.restricted'));
  }
  assert.doesNotMatch(html, /on(click|touchstart)=/i);
});
