/**
 * Notification text-to-speech — thin, defensive wrapper around the three
 * engines the Voice setting can pick from. Headless/CI environments (and
 * Bun's test runner) have no `window` at all, and some real browsers ship
 * `speechSynthesis` in a half-broken state, so every entry point here is
 * guarded to never throw: TTS is a nice-to-have, not something that should
 * crash the cockpit.
 *
 * Voice modes (persisted in localStorage, chosen in Settings → App):
 *   - "auto"            — detect Thai script per message, speak via the OS
 *                         voice (`speechSynthesis`). Zero downloads.
 *   - "thai-cloud" /    — Google Translate TTS (the long-stable
 *     "english-cloud"     `translate_tts` endpoint, ~200-char cap per
 *                         request). Needs internet; falls back to the OS
 *                         voice when offline or blocked.
 *   - "english-offline" — Piper neural TTS in-browser (WASM + ONNX,
 *                         `en_US-hfc_female-medium`). The model downloads
 *                         once (~60 MB) and is cached in OPFS, so it is
 *                         offline after first use. Falls back to the OS
 *                         voice on any failure.
 *   - "thai-offline"    — the OS voice with `lang=th-TH`. Piper's voice
 *                         catalog has NO Thai model (checked the full
 *                         123-voice list, 2026-07), and the community Thai
 *                         VITS models (PyThaiNLP, MMS) are not
 *                         piper-phonemize compatible — so the OS/espeak
 *                         Thai voice is the only true-offline option today.
 *
 * Mute preference persists in localStorage so it survives reloads; that
 * lookup is guarded too (private browsing / storage quota can throw on
 * `getItem`/`setItem`).
 */

/** Only warning/critical notifications are spoken by default — info is too chatty. */
export const SPOKEN_SEVERITIES = new Set(["warning", "critical"]);

const MUTE_STORAGE_KEY = "rtn-gcs.notifications.speechMuted";
const VOICE_LANG_STORAGE_KEY = "rtn-gcs.notifications.voiceLang";
const VOICE_SPEED_STORAGE_KEY = "rtn-gcs.notifications.voiceSpeed";

export type VoiceMode =
  | "auto"
  | "thai-cloud"
  | "english-cloud"
  | "thai-offline"
  | "english-offline";

/** Google Translate TTS rejects long inputs; notifications are short anyway. */
const CLOUD_TEXT_LIMIT = 200;

/** The one Piper voice we use — English (US), medium quality, in the official catalog. */
const PIPER_ENGLISH_VOICE = "en_US-hfc_female-medium";

/**
 * If Piper hasn't produced audio by this deadline the OS voice speaks the
 * notification instead — an alert must not wait on a model download (the
 * library's fetch has no timeout and hangs when HuggingFace is
 * unreachable). The download keeps going in the background so a later
 * notification finds the model cached.
 */
const PIPER_DEADLINE_MS = 10_000;

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

/** Read the persisted voice mode. Defaults to "auto" if unset or unreadable. */
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

/** Persist the voice mode. */
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

/** Read the persisted voice speed. Defaults to 1.0 if unset or unreadable. */
export function loadVoiceSpeed(): number {
  if (typeof window === "undefined" || !window.localStorage) {
    return 1.0;
  }
  try {
    const val = window.localStorage.getItem(VOICE_SPEED_STORAGE_KEY);
    return val ? parseFloat(val) : 1.0;
  } catch {
    return 1.0;
  }
}

/** Persist the voice speed. */
export function saveVoiceSpeed(speed: number): void {
  if (typeof window === "undefined" || !window.localStorage) {
    return;
  }
  try {
    window.localStorage.setItem(VOICE_SPEED_STORAGE_KEY, speed.toString());
  } catch {
    // ignore
  }
}

/**
 * Speak `text` according to the persisted voice mode. Never throws — any
 * engine failure falls back to the OS voice, and OS-voice failures are
 * swallowed.
 */
export function speak(text: string): void {
  if (typeof window === "undefined") {
    return;
  }

  const mode = loadVoiceLanguage();
  const speed = loadVoiceSpeed();
  const thai = mode === "auto" ? /[ก-๙]/.test(text) : mode.startsWith("thai");
  const lang = thai ? "th-TH" : "en-US";

  if (mode === "thai-cloud" || mode === "english-cloud") {
    speakCloud(text, lang, speed);
  } else if (mode === "english-offline") {
    speakPiper(text, lang, speed);
  } else {
    // "auto" and "thai-offline" (see the header — Piper has no Thai voice).
    speakLocal(text, lang, speed);
  }
}

/**
 * Cloud mode: Vercel TTS endpoint. Media elements aren't
 * CORS-restricted for playback, so `new Audio(url)` works cross-origin.
 * Any failure (offline, rate-limited, blocked) drops to the OS voice.
 */
function speakCloud(text: string, lang: string, speed: number): void {
  if (typeof window.Audio === "undefined" || window.navigator.onLine === false) {
    speakLocal(text, lang, speed);
    return;
  }
  try {
    const q = encodeURIComponent(text.slice(0, CLOUD_TEXT_LIMIT));
    const url = `https://tts-api.vercel.app/api/tts?text=${q}`;
    const audio = new window.Audio(url);
    audio.playbackRate = speed;
    playWithFallback(audio, () => speakLocal(text, lang, speed));
  } catch {
    speakLocal(text, lang, speed);
  }
}

/**
 * Offline-English mode: Piper neural TTS running in a worker (WASM
 * phonemizer + onnxruntime-web). The voice model is fetched from
 * HuggingFace on first use and cached in OPFS, so later calls are fully
 * offline. Everything is lazy-imported so the ONNX runtime never loads
 * unless this mode is actually selected.
 */
function speakPiper(text: string, lang: string, speed: number): void {
  let fellBack = false;
  const fallBack = () => {
    if (!fellBack) {
      fellBack = true;
      speakLocal(text, lang, speed);
    }
  };
  const deadline = window.setTimeout(fallBack, PIPER_DEADLINE_MS);

  const run = async () => {
    try {
      const tts = await import("@mintplex-labs/piper-tts-web");
      const wav = await tts.predict({ text, voiceId: PIPER_ENGLISH_VOICE });
      window.clearTimeout(deadline);
      if (fellBack) {
        // The OS voice already spoke this one; the model is now cached
        // (OPFS), so the next notification gets the Piper voice.
        return;
      }
      const audio = new window.Audio();
      audio.playbackRate = speed;
      audio.src = URL.createObjectURL(wav);
      audio.onended = () => URL.revokeObjectURL(audio.src);
      playWithFallback(audio, () => {
        URL.revokeObjectURL(audio.src);
        fallBack();
      });
    } catch (err) {
      window.clearTimeout(deadline);
      console.warn("Piper TTS failed, using OS voice:", err);
      fallBack();
    }
  };
  void run();
}

/** Play `audio`, invoking `fallback` at most once if playback fails. */
function playWithFallback(audio: HTMLAudioElement, fallback: () => void): void {
  let done = false;
  const fail = () => {
    if (!done) {
      done = true;
      fallback();
    }
  };
  audio.onerror = fail;
  audio.play().catch(fail);
}

/** The OS voice (`speechSynthesis`) — final fallback for every mode. */
function speakLocal(text: string, lang: string, speed: number): void {
  if (!isSpeechSupported()) {
    return;
  }
  try {
    const utterance = new window.SpeechSynthesisUtterance(text);
    utterance.lang = lang;
    utterance.rate = speed;
    window.speechSynthesis.speak(utterance);
  } catch {
    // TTS is best-effort — never let it break the app.
  }
}
