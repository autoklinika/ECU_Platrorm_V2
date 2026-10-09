// Future read-only domain payload helper. Descriptions are supplied by an
// authorized backend/catalog, never inferred by the browser from the DTC code.
// Keep this independent of interface-label catalogs and DUT protocols.
function baseLanguage(value) {
  return typeof value === "string" ? value.split("-")[0].toLowerCase() : "";
}

export function selectLocalizedText(translations, requestedLocale, fallbackLocale = "en") {
  if (!translations || typeof translations !== "object") {
    return { text: null, locale: null, isFallback: true };
  }
  const candidates = [
    requestedLocale,
    baseLanguage(requestedLocale),
    fallbackLocale,
    baseLanguage(fallbackLocale)
  ];
  for (const language of new Set(candidates)) {
    if (typeof language !== "string" || language.length === 0) continue;
    if (Object.hasOwn(translations, language)
        && typeof translations[language] === "string"
        && translations[language].trim() !== "") {
      return {
        text: translations[language],
        locale: language,
        isFallback: language !== requestedLocale && language !== baseLanguage(requestedLocale)
      };
    }
  }
  return { text: null, locale: null, isFallback: true };
}
