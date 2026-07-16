import i18n from "i18next";
import { initReactI18next } from "react-i18next";
import LanguageDetector from "i18next-browser-languagedetector";

import translationEN from "./locales/en/translation.json";
import translationTH from "./locales/th/translation.json";
import crowdinTH from "./locales/th/crowdin.json";

const resources = {
  en: {
    translation: translationEN
  },
  th: {
    translation: {
      ...crowdinTH,
      ...translationTH
    }
  }
};

i18n
  .use(LanguageDetector)
  .use(initReactI18next)
  .init({
    resources,
    fallbackLng: "en",
    interpolation: {
      escapeValue: false
    }
  });

export default i18n;
