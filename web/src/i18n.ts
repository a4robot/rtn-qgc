import i18n from "i18next";
import { initReactI18next } from "react-i18next";
import LanguageDetector from "i18next-browser-languagedetector";

const resources = {
  en: {
    translation: {
      "App settings": "App settings",
      "UI Language": "UI Language",
      "Voice Language": "Voice Language",
      "Auto-Detect": "Auto-Detect",
      "English": "English",
      "Thai": "Thai"
    }
  },
  th: {
    translation: {
      "App settings": "การตั้งค่าแอปพลิเคชัน",
      "UI Language": "ภาษาของหน้าจอ (UI Language)",
      "Voice Language": "ภาษาของเสียงเตือน (Voice Language)",
      "Auto-Detect": "อัตโนมัติ (Auto-Detect)",
      "English": "อังกฤษ (English)",
      "Thai": "ไทย (Thai)"
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
