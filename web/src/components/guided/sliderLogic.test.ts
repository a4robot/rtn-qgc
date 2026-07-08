import { expect, test } from "bun:test";

import {
  CONFIRM_FRACTION,
  clampFraction,
  fractionFromDelta,
  fractionFromHoldElapsed,
  HOLD_MS,
  shouldConfirm,
} from "./sliderLogic.ts";

test("clampFraction clamps below 0 and above 1", () => {
  expect(clampFraction(-0.5)).toBe(0);
  expect(clampFraction(1.5)).toBe(1);
  expect(clampFraction(0.42)).toBeCloseTo(0.42);
});

test("clampFraction treats NaN as 0", () => {
  expect(clampFraction(Number.NaN)).toBe(0);
});

test("fractionFromDelta moves proportionally to drag distance over travel", () => {
  expect(fractionFromDelta(0, 50, 100)).toBeCloseTo(0.5);
  expect(fractionFromDelta(0.2, 20, 100)).toBeCloseTo(0.4);
});

test("fractionFromDelta clamps overshoot in both directions", () => {
  expect(fractionFromDelta(0, 500, 100)).toBe(1);
  expect(fractionFromDelta(0.5, -1000, 100)).toBe(0);
});

test("fractionFromDelta with zero/negative travel holds the start fraction", () => {
  expect(fractionFromDelta(0.3, 40, 0)).toBe(0.3);
  expect(fractionFromDelta(0.3, 40, -10)).toBe(0.3);
});

test("shouldConfirm is false just under the threshold and true at/over it", () => {
  expect(shouldConfirm(CONFIRM_FRACTION - 0.01)).toBe(false);
  expect(shouldConfirm(CONFIRM_FRACTION)).toBe(true);
  expect(shouldConfirm(1)).toBe(true);
  expect(shouldConfirm(0)).toBe(false);
});

test("fractionFromHoldElapsed ramps linearly across the hold window", () => {
  expect(fractionFromHoldElapsed(0, 1000)).toBe(0);
  expect(fractionFromHoldElapsed(500, 1000)).toBeCloseTo(0.5);
  expect(fractionFromHoldElapsed(1000, 1000)).toBe(1);
});

test("fractionFromHoldElapsed clamps past the hold window and defaults to HOLD_MS", () => {
  expect(fractionFromHoldElapsed(2000, 1000)).toBe(1);
  expect(fractionFromHoldElapsed(HOLD_MS)).toBe(1);
});
