import { expect, test } from "bun:test";

import { formatRelativeTime } from "./relativeTime.ts";

const NOW = 1_767_690_005_000;

/**
 * i18next-shaped `t` stand-in: returns the key with `{{count}}`
 * interpolated, i.e. exactly what the real `t` produces for the English
 * locale — so the expected strings below stay readable English.
 */
const t = (key: string, options?: { count?: number }): string =>
  key.replace("{{count}}", String(options?.count));

test("under 5s reads 'just now'", () => {
  expect(formatRelativeTime(NOW - 2000, t, NOW)).toBe("just now");
});

test("seconds bucket", () => {
  expect(formatRelativeTime(NOW - 42_000, t, NOW)).toBe("42s ago");
});

test("minutes bucket", () => {
  expect(formatRelativeTime(NOW - 5 * 60_000, t, NOW)).toBe("5m ago");
});

test("hours bucket", () => {
  expect(formatRelativeTime(NOW - 3 * 3_600_000, t, NOW)).toBe("3h ago");
});

test("days bucket", () => {
  expect(formatRelativeTime(NOW - 2 * 86_400_000, t, NOW)).toBe("2d ago");
});

test("clamps a future receivedAtMs (clock skew) to 'just now' rather than negative", () => {
  expect(formatRelativeTime(NOW + 5000, t, NOW)).toBe("just now");
});
