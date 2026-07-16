import i18n from "../../i18n.ts";

/**
 * Notification text-to-speech — thin, defensive wrapper around
 * `window.speechSynthesis`. Headless/CI environments (and Bun's test runner)
 * have no `window` at all, and some real browsers ship `speechSynthesis` in
 * a half-broken state, so every entry point here is guarded to never throw:
 * TTS is a nice-to-have, not something that should crash the cockpit.
 *
 * Mute preference persists in localStorage so it survives reloads; that
 * lookup is guarded too (private browsing / storage quota can throw on
 * `getItem`/`setItem`).
 */

/** Only warning/critical notifications are spoken by default — info is too chatty. */
export const SPOKEN_SEVERITIES = new Set(["warning", "critical"]);

const MUTE_STORAGE_KEY = "rtn-gcs.notifications.speechMuted";
const VOICE_LANG_STORAGE_KEY = "rtn-gcs.notifications.voiceLang";

/** True if the browser's speechSynthesis API is present and callable. */
export function isSpeechSupported(): boolean {
  return (
    typeof window !== "undefined" &&
    "speechSynthesis" in window &&
    typeof window.speechSynthesis?.speak === "function" &&
    typeof window.SpeechSynthesisUtterance === "function"
  );
}

/** Read the persisted mute preference. Defaults to false (unmuted) if unset or unreadable. */
export function loadSpeechMuted(): boolean {
  if (typeof window === "undefined" || !window.localStorage) {
    return false;
  }
  try {
    return window.localStorage.getItem(MUTE_STORAGE_KEY) === "1";
  } catch {
    return false;
  }
}

/** Persist the mute preference. Silently no-ops if storage is unavailable. */
export function saveSpeechMuted(muted: boolean): void {
  if (typeof window === "undefined" || !window.localStorage) {
    return;
  }
  try {
    window.localStorage.setItem(MUTE_STORAGE_KEY, muted ? "1" : "0");
  } catch {
    // private mode / quota exceeded — mute preference just won't persist.
  }
}

/** Read the persisted voice language preference. Defaults to "auto" if unset or unreadable. */
export function loadVoiceLanguage(): string {
  if (typeof window === "undefined" || !window.localStorage) {
    return "auto";
  }
  try {
    return window.localStorage.getItem(VOICE_LANG_STORAGE_KEY) || "auto";
  } catch {
    return "auto";
  }
}

/** Persist the voice language preference. */
export function saveVoiceLanguage(lang: string): void {
  if (typeof window === "undefined" || !window.localStorage) {
    return;
  }
  try {
    window.localStorage.setItem(VOICE_LANG_STORAGE_KEY, lang);
  } catch {
    // ignore
  }
}

/**
 * Speak `text` if supported. Never throws — any speechSynthesis failure
 * (unsupported voice, engine crash, etc.) is swallowed.
 */
export function speak(text: string): void {
  if (typeof window === "undefined") {
    return;
  }

  const voiceLang = loadVoiceLanguage();
  // If auto, we let the OS/API decide, or fallback to Thai if Thai chars detected
  const resolvedLang = voiceLang !== "auto" ? voiceLang : (/[ก-๙]/.test(text) ? "th-TH" : "");

  // Append lang parameter if needed
  if (typeof window.Audio !== "undefined" && window.navigator.onLine !== false) {
    const langParam = resolvedLang ? `&lang=${resolvedLang}` : "";
    const url = `https://tts-api.vercel.app/api/tts?text=${encodeURIComponent(text)}${langParam}`;
    const audio = new window.Audio(url);
    
    let fallbackTriggered = false;
    const doFallback = () => {
      if (!fallbackTriggered) {
        fallbackTriggered = true;
        fallbackToLocalTTS(text, resolvedLang);
      }
    };

    audio.onerror = doFallback;
    audio.play().catch(doFallback);
  } else {
    fallbackToLocalTTS(text, resolvedLang);
  }
}

function fallbackToLocalTTS(text: string, lang: string): void {
  if (!isSpeechSupported()) {
    return;
  }
  try {
    const utterance = new window.SpeechSynthesisUtterance(text);
    if (lang) {
      utterance.lang = lang;
    }
    window.speechSynthesis.speak(utterance);
  } catch {
    // TTS is best-effort — never let it break the app.
  }
}
