/**
 * Pure formatting helpers for the FlyView instrument strip (Instruments.tsx).
 *
 * Kept separate from the component so the null-handling/formatting logic
 * (PROTOCOL.md §4: every telemetry leaf is `number | null`) can be unit
 * tested without rendering React.
 */

import type { GpsFix } from "../../bridge/types.ts";

/** §4: unknown values are null — render an em dash instead of crashing. */
export function fmtNum(value: number | null, digits: number): string {
  return value === null ? "—" : value.toFixed(digits);
}

/** Wrap a heading/yaw value into [0, 360). */
export function normalizeHeading(deg: number): number {
  const wrapped = deg % 360;
  return wrapped < 0 ? wrapped + 360 : wrapped;
}

/** Zero-padded 3-digit heading string, e.g. `007`, `271`. Null-safe. */
export function fmtHeading(deg: number | null): string {
  if (deg === null) {
    return "—";
  }
  return String(Math.round(normalizeHeading(deg)) % 360).padStart(3, "0");
}

const CARDINALS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"] as const;

/** Nearest 8-point cardinal/intercardinal letter for a heading. Null-safe. */
export function headingCardinal(deg: number | null): string {
  if (deg === null) {
    return "—";
  }
  const normalized = normalizeHeading(deg);
  const index = Math.round(normalized / 45) % 8;
  return CARDINALS[index] ?? "N";
}

export type ClimbTendency = "up" | "down" | "level";

/** Climb-rate tendency arrow direction; |rate| below the threshold reads level. */
export function climbTendency(climbRate: number | null, thresholdMs = 0.1): ClimbTendency {
  if (climbRate === null) {
    return "level";
  }
  if (climbRate > thresholdMs) {
    return "up";
  }
  if (climbRate < -thresholdMs) {
    return "down";
  }
  return "level";
}

/** Tendency glyph for display: ▲ climbing, ▼ descending, — level/unknown. */
export function climbGlyph(climbRate: number | null, thresholdMs = 0.1): string {
  const tendency = climbTendency(climbRate, thresholdMs);
  if (tendency === "up") return "▲";
  if (tendency === "down") return "▼";
  return "—";
}

/** Signed climb-rate string with a leading `+` for non-negative values. Null-safe. */
export function fmtSigned(value: number | null, digits = 1): string {
  if (value === null) {
    return "—";
  }
  const fixed = value.toFixed(digits);
  return value >= 0 ? `+${fixed}` : fixed;
}

/** Compact fix-quality labels for the instrument strip (shorter than Status.tsx's). */
const GPS_FIX_SHORT_LABELS: Record<GpsFix, string> = {
  none: "NO FIX",
  "2d": "2D",
  "3d": "3D",
  rtkFloat: "RTK-F",
  rtkFixed: "RTK",
};

export function gpsFixShortLabel(fix: GpsFix): string {
  return GPS_FIX_SHORT_LABELS[fix] ?? fix;
}

export type BatteryLevel = "ok" | "warn" | "critical" | "unknown";

/** >50 ok, 20-50 warn, <20 critical; null reads unknown (neutral styling). */
export function batteryLevel(percent: number | null): BatteryLevel {
  if (percent === null) {
    return "unknown";
  }
  if (percent < 20) return "critical";
  if (percent < 50) return "warn";
  return "ok";
}
