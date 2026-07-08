/**
 * Pure validation for a parameter edit — shared by {@link ParamEdit} and its
 * tests. Takes the raw text from the numeric input plus the param's meta
 * (PROTOCOL.md §6.1) and decides whether it's a value the bridge should be
 * asked to set.
 *
 * Null-safe: `meta` itself may be missing, and any field on it may be
 * `null` (PROTOCOL.md §6.1 — only fields the firmware actually reports are
 * populated).
 */

import type { ParamMeta } from "../../bridge/types.ts";

/** Integer `ParamMeta.type` values — everything else (float/double) allows fractions. */
const INTEGER_TYPES: ReadonlySet<ParamMeta["type"]> = new Set([
  "int8",
  "uint8",
  "int16",
  "uint16",
  "int32",
  "uint32",
]);

export type ParamValidation =
  | { ok: true; value: number }
  | { ok: false; error: string };

/**
 * Validate raw text input for a parameter against its meta.
 *
 * Checks, in order: non-empty, parses as a finite number, integer-ness for
 * integer `type`s, and `meta.min`/`meta.max` bounds (when present).
 */
export function validateParamValue(
  raw: string,
  meta: ParamMeta | null | undefined,
): ParamValidation {
  const trimmed = raw.trim();
  if (trimmed === "") {
    return { ok: false, error: "Value is required" };
  }

  const parsed = Number(trimmed);
  if (!Number.isFinite(parsed)) {
    return { ok: false, error: "Must be a number" };
  }

  if (meta?.type != null && INTEGER_TYPES.has(meta.type) && !Number.isInteger(parsed)) {
    return { ok: false, error: `Must be an integer (${meta.type})` };
  }

  if (meta?.min != null && parsed < meta.min) {
    return { ok: false, error: `Must be ≥ ${meta.min}` };
  }

  if (meta?.max != null && parsed > meta.max) {
    return { ok: false, error: `Must be ≤ ${meta.max}` };
  }

  return { ok: true, value: parsed };
}
