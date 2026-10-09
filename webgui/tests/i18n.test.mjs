import test from "node:test";
import assert from "node:assert/strict";
import {
  CATALOGS, DEFAULT_LOCALE, SUPPORTED_LOCALES, STORAGE_KEY,
  normalizeLocale, translate, readLocale, storeLocale
} from "../src/i18n.mjs";
import { selectLocalizedText } from "../src/domain-text.mjs";

test("English is the deterministic initial language", () => {
  assert.equal(DEFAULT_LOCALE, "en");
  assert.equal(normalizeLocale(undefined), "en");
  assert.equal(normalizeLocale("de"), "en");
  assert.equal(normalizeLocale("en-US"), "en"); // no automatic browser negotiation
  assert.equal(translate("en", "home.tests"), "TESTS");
  assert.equal(translate("pl", "home.tests"), "TESTY");
  assert.equal(translate("pl", "settings.language"), "JĘZYK");
});

test("all supported UI catalogs have identical keys and nonempty values", () => {
  assert.deepEqual(SUPPORTED_LOCALES, ["en", "pl"]);
  const baseline = Object.keys(CATALOGS.en).sort();
  for (const [locale, catalog] of Object.entries(CATALOGS)) {
    assert.deepEqual(Object.keys(catalog).sort(), baseline, locale);
    for (const [key, value] of Object.entries(catalog)) {
      assert.equal(typeof value, "string", locale + "/" + key);
      assert.ok(value.trim().length > 0, locale + "/" + key);
    }
  }
});

test("missing translation falls back to English, never crashes", () => {
  assert.equal(translate("de", "home.heading"), "Main menu");
  assert.equal(translate("pl", "future.unlisted"), "future.unlisted");
});

test("language preference is local, optional and resilient to denied storage", () => {
  const state = new Map();
  const storage = {
    getItem: (key) => state.get(key) ?? null,
    setItem: (key, value) => { state.set(key, value); }
  };
  assert.equal(readLocale(storage), "en");
  assert.equal(storeLocale(storage, "pl"), "pl");
  assert.equal(state.get(STORAGE_KEY), "pl");
  assert.equal(readLocale(storage), "pl");
  assert.equal(storeLocale(storage, "bogus"), "en");

  const blocked = {
    getItem: () => { throw new Error("storage denied"); },
    setItem: () => { throw new Error("storage denied"); }
  };
  assert.equal(readLocale(blocked), "en");
  assert.equal(storeLocale(blocked, "pl"), "pl");
});

test("domain/DTC descriptions have independent multilingual fallback", () => {
  const source = Object.freeze({
    en: "Reference-language description",
    pl: "Opis w języku polskim"
  });
  assert.deepEqual(selectLocalizedText(source, "pl"), {
    text: "Opis w języku polskim", locale: "pl", isFallback: false
  });
  assert.deepEqual(selectLocalizedText(source, "pl-PL"), {
    text: "Opis w języku polskim", locale: "pl", isFallback: false
  });
  assert.deepEqual(selectLocalizedText(source, "de"), {
    text: "Reference-language description", locale: "en", isFallback: true
  });
  assert.deepEqual(selectLocalizedText({pl: "Tylko PL"}, "en"), {
    text: null, locale: null, isFallback: true
  });
  assert.deepEqual(selectLocalizedText(null, "pl"), {
    text: null, locale: null, isFallback: true
  });
});
