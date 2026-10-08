import en from "./locales/en.mjs";
import pl from "./locales/pl.mjs";

export const DEFAULT_LOCALE = "en";
export const STORAGE_KEY = "ecu-bench-webgui.locale.v1";

// Register new UI languages here; each catalog uses stable message keys.
// Domain/ECU/DTC text is deliberately excluded.
export const CATALOGS = Object.freeze({ en, pl });
export const SUPPORTED_LOCALES = Object.freeze(Object.keys(CATALOGS));

export function normalizeLocale(value) {
  return typeof value === "string" && Object.hasOwn(CATALOGS, value)
    ? value : DEFAULT_LOCALE;
}

export function translate(locale, key) {
  const catalog = CATALOGS[normalizeLocale(locale)];
  const message = catalog[key] ?? CATALOGS[DEFAULT_LOCALE][key];
  return typeof message === "string" ? message : key;
}

export function readLocale(storage) {
  try {
    return normalizeLocale(storage.getItem(STORAGE_KEY));
  } catch {
    return DEFAULT_LOCALE;
  }
}

export function storeLocale(storage, locale) {
  const normalized = normalizeLocale(locale);
  try {
    storage.setItem(STORAGE_KEY, normalized);
  } catch {
    // Private browsing and locked-down kiosks may disable storage.
  }
  return normalized;
}

export function applyTranslations(root, locale) {
  root.querySelectorAll("[data-i18n]").forEach((element) => {
    element.textContent = translate(locale, element.dataset.i18n);
  });
  root.querySelectorAll("[data-i18n-aria-label]").forEach((element) => {
    element.setAttribute("aria-label",
      translate(locale, element.dataset.i18nAriaLabel));
  });
}
