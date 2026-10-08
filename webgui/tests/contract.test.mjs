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
  assert.doesNotMatch(css, /(?:linear|radial|conic)-gradient\(|backdrop-filter|@keyframes/i);
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
  assert.match(html, /id="api-token" type="password" autocomplete="off"/);
  assert.match(html, /connect-src 'self' http:\/\/127\.0\.0\.1:8878/);
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
    assert.match(key, /^[a-z]+\.[A-Za-z]+$/);
    assert.ok(Object.hasOwn(CATALOGS.en, key), "Missing English UI key: " + key);
  }
});
