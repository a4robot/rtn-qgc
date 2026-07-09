import { expect, test } from "bun:test";

import { isSpeechSupported, loadSpeechMuted, saveSpeechMuted, speak, SPOKEN_SEVERITIES } from "./speech.ts";

// Bun's test runner has no `window`/`speechSynthesis`/`localStorage` — the
// same shape as a headless-chromium screenshot harness without TTS support.
// Every entry point must degrade gracefully (never throw) in that environment.

test("isSpeechSupported is false with no window (headless/CI)", () => {
  expect(isSpeechSupported()).toBe(false);
});

test("loadSpeechMuted defaults to false with no window/localStorage", () => {
  expect(loadSpeechMuted()).toBe(false);
});

test("saveSpeechMuted does not throw with no window/localStorage", () => {
  expect(() => saveSpeechMuted(true)).not.toThrow();
});

test("speak does not throw when speechSynthesis is unsupported", () => {
  expect(() => speak("engine failure")).not.toThrow();
});

test("SPOKEN_SEVERITIES covers warning and critical but not info", () => {
  expect(SPOKEN_SEVERITIES.has("warning")).toBe(true);
  expect(SPOKEN_SEVERITIES.has("critical")).toBe(true);
  expect(SPOKEN_SEVERITIES.has("info")).toBe(false);
});
