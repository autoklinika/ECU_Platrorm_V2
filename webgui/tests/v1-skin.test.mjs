import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const read = (name) => readFileSync(new URL("../" + name, import.meta.url), "utf8");
const css = read("styles.css");
const html = read("index.html");
const app = read("src/app.mjs");
const v1 = (selector) => css.slice(css.indexOf(selector), css.indexOf("}", css.indexOf(selector)) + 1);

test("TESTS theme consumes the original WebGUI V1 palette and flat tile contract", () => {
  for (const value of [
    "--page-bg: #f1f3f6", "--panel-bg: #f7f8fa",
    "--bar-bg: #e5eaf0", "--border: #b2bfcb",
    "--rule: #c5ced8", "--blue: #226da8"
  ]) assert.ok(css.includes(value), value);
  const tile = v1(".catalog-page--selection .tile {");
  assert.match(tile, /min-height: 168px/);
  assert.match(tile, /border-top: 3px solid #668eb4/);
  assert.match(tile, /border-radius: 2px/);
  assert.match(tile, /background: var\(--panel-bg\)/);
  assert.match(tile, /color: var\(--ink\)/);
  assert.doesNotMatch(css, /#59c8ff|#2a84c9|#7dd6ff|border-radius:\s*(?:16|20|22|26)px/i);
});

test("all TESTS catalog sections reuse V1 page conventions, without changing home", () => {
  for (const page of ["tests", "test-truck", "test-agri", "test-ohv",
    "truck-man", "truck-daf", "truck-scania", "truck-iveco", "truck-mb"]) {
    assert.match(html, new RegExp(
      'class="page catalog-page catalog-page--selection" data-page="' + page + '" hidden'
    ), page);
  }
  for (const page of ["daf-sac", "sac-dtc", "sac-activations", "sac-programming"]) {
    assert.match(html, new RegExp(
      'class="page catalog-page catalog-page--sac" data-page="' + page + '" hidden'
    ), page);
  }
  assert.match(html, /class="page" data-page="home"/);
  assert.match(css, /\.catalog-page > h1 \{[\s\S]*?border-bottom: 1px solid var\(--rule\)/);
  assert.match(css, /\.catalog-page--selection \.back-button \{[\s\S]*?background: var\(--panel-bg\)/);
  assert.doesNotMatch(html, /legacy-page|legacy-parameter/);
});

test("the four SAC rows remain a single bounded, flat V1 measurement panel", () => {
  const panel = v1(".sac-parameter-panel {");
  assert.match(panel, /width: min\(100%, 1080px\)/);
  assert.match(panel, /border-top: 3px solid #668eb4/);
  assert.match(panel, /border-radius: 2px/);
  assert.match(panel, /background: var\(--panel-bg\)/);
  const row = v1(".sac-parameter-row {");
  assert.match(row, /min-height: 54px/);
  for (const name of ["sac-pressure-1", "sac-pressure-2",
    "sac-permanent-voltage", "sac-ignition-voltage"]) {
    assert.match(html, new RegExp('id="' + name + '">—</output>'));
  }
  assert.match(app, /for \(const name of \["sac-permanent-voltage", "sac-ignition-voltage"/);
  assert.match(html, /data-i18n="sac.noReadout"/);
});

test("V1 left drawer, DTC details and responsive rules do not add ECU control", () => {
  assert.match(css, /\.side-panel:has\(#sac-side-navigation:not\(\[hidden\]\)\)/);
  const action = v1(".side-navigation button {");
  assert.match(action, /min-height: 72px/);
  assert.match(action, /background: var\(--panel-bg\)/);
  assert.match(action, /border-radius: 2px/);
  assert.match(css, /\.side-navigation button\[aria-current="page"\]/);
  assert.match(css, /\.dtc-detail-dialog \{[\s\S]*?background: var\(--panel-bg\)/);
  assert.match(css, /@media \(max-width: 860px\)/);
  assert.match(css, /@media \(max-width: 610px\)/);
  assert.match(html, /data-page="sac-programming"[\s\S]*?sac.restricted/);
  assert.match(html, /data-page="sac-activations"[\s\S]*?sac.restricted/);
  assert.doesNotMatch(app, /startDTCRead|clearDTC|CockpitController|SystemController|navigator\.serial|navigator\.usb/);
  assert.doesNotMatch(html, /on(?:click|touchstart)=/i);
});
