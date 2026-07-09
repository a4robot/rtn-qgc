import { expect, test } from "bun:test";

import { formatRelativeTime } from "./relativeTime.ts";

const NOW = 1_767_690_005_000;

test("under 5s reads 'just now'", () => {
  expect(formatRelativeTime(NOW - 2000, NOW)).toBe("just now");
});

test("seconds bucket", () => {
  expect(formatRelativeTime(NOW - 42_000, NOW)).toBe("42s ago");
});

test("minutes bucket", () => {
  expect(formatRelativeTime(NOW - 5 * 60_000, NOW)).toBe("5m ago");
});

test("hours bucket", () => {
  expect(formatRelativeTime(NOW - 3 * 3_600_000, NOW)).toBe("3h ago");
});

test("days bucket", () => {
  expect(formatRelativeTime(NOW - 2 * 86_400_000, NOW)).toBe("2d ago");
});

test("clamps a future receivedAtMs (clock skew) to 'just now' rather than negative", () => {
  expect(formatRelativeTime(NOW + 5000, NOW)).toBe("just now");
});
