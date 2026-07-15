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

/**
 * Speak `text` if supported. Never throws — any speechSynthesis failure
 * (unsupported voice, engine crash, etc.) is swallowed.
 */
export function speak(text: string): void {
  if (typeof window === "undefined") {
    return;
  }

  // Append lang parameter if needed, but the online TTS might auto-detect
  if (typeof window.Audio !== "undefined" && window.navigator.onLine !== false) {
    const langParam = i18n.language ? `&lang=${i18n.language}` : "";
    const url = `https://tts-api.vercel.app/api/tts?text=${encodeURIComponent(text)}${langParam}`;
    const audio = new window.Audio(url);
    
    let fallbackTriggered = false;
    const doFallback = () => {
      if (!fallbackTriggered) {
        fallbackTriggered = true;
        fallbackToLocalTTS(text);
      }
    };

    audio.onerror = doFallback;
    audio.play().catch(doFallback);
  } else {
    fallbackToLocalTTS(text);
  }
}

function fallbackToLocalTTS(text: string): void {
  if (!isSpeechSupported()) {
    return;
  }
  try {
    const utterance = new window.SpeechSynthesisUtterance(text);
    
    // Use the language selected in settings
    if (i18n.language) {
      utterance.lang = i18n.language;
    }
    // Fallback to force Thai if there are Thai characters
    if (/[ก-๙]/.test(text)) {
      utterance.lang = "th-TH";
    }
    
    window.speechSynthesis.speak(utterance);
  } catch {
    // TTS is best-effort — never let it break the app.
  }
}

