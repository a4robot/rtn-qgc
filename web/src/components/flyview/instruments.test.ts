import { expect, test } from "bun:test";

import {
  batteryLevel,
  climbGlyph,
  climbTendency,
  fmtHeading,
  fmtNum,
  fmtSigned,
  gpsFixShortLabel,
  headingCardinal,
  normalizeHeading,
} from "./instruments.ts";

test("fmtNum renders em dash for null and fixed digits otherwise", () => {
  expect(fmtNum(null, 1)).toBe("—");
  expect(fmtNum(3.14159, 2)).toBe("3.14");
  expect(fmtNum(0, 1)).toBe("0.0");
});

test("normalizeHeading wraps into [0, 360)", () => {
  expect(normalizeHeading(0)).toBe(0);
  expect(normalizeHeading(359.9)).toBeCloseTo(359.9);
  expect(normalizeHeading(360)).toBe(0);
  expect(normalizeHeading(-10)).toBe(350);
  expect(normalizeHeading(720 + 5)).toBe(5);
});

test("fmtHeading zero-pads to 3 digits and is null-safe", () => {
  expect(fmtHeading(null)).toBe("—");
  expect(fmtHeading(7)).toBe("007");
  expect(fmtHeading(271.4)).toBe("271");
  expect(fmtHeading(359.6)).toBe("000");
  expect(fmtHeading(-5)).toBe("355");
});

test("headingCardinal maps to nearest 8-point compass letter", () => {
  expect(headingCardinal(null)).toBe("—");
  expect(headingCardinal(0)).toBe("N");
  expect(headingCardinal(44)).toBe("NE");
  expect(headingCardinal(90)).toBe("E");
  expect(headingCardinal(180)).toBe("S");
  expect(headingCardinal(315)).toBe("NW");
  expect(headingCardinal(359)).toBe("N");
});

test("climbTendency/climbGlyph classify by threshold and are null-safe", () => {
  expect(climbTendency(null)).toBe("level");
  expect(climbTendency(0)).toBe("level");
  expect(climbTendency(0.05)).toBe("level");
  expect(climbTendency(0.2)).toBe("up");
  expect(climbTendency(-0.2)).toBe("down");

  expect(climbGlyph(null)).toBe("—");
  expect(climbGlyph(1.5)).toBe("▲");
  expect(climbGlyph(-1.5)).toBe("▼");
});

test("fmtSigned adds a leading + for non-negative values, em dash for null", () => {
  expect(fmtSigned(null)).toBe("—");
  expect(fmtSigned(0)).toBe("+0.0");
  expect(fmtSigned(1.23)).toBe("+1.2");
  expect(fmtSigned(-1.23)).toBe("-1.2");
});

test("gpsFixShortLabel covers all §4 fix strings", () => {
  expect(gpsFixShortLabel("none")).toBe("NO FIX");
  expect(gpsFixShortLabel("2d")).toBe("2D");
  expect(gpsFixShortLabel("3d")).toBe("3D");
  expect(gpsFixShortLabel("rtkFloat")).toBe("RTK-F");
  expect(gpsFixShortLabel("rtkFixed")).toBe("RTK");
});

test("batteryLevel thresholds and null-safety", () => {
  expect(batteryLevel(null)).toBe("unknown");
  expect(batteryLevel(19)).toBe("critical");
  expect(batteryLevel(20)).toBe("warn");
  expect(batteryLevel(49)).toBe("warn");
  expect(batteryLevel(50)).toBe("ok");
  expect(batteryLevel(100)).toBe("ok");
});
