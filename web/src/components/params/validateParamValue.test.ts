import { describe, expect, test } from "bun:test";

import type { ParamMeta } from "../../bridge/types.ts";
import { validateParamValue } from "./validateParamValue.ts";

function makeMeta(overrides: Partial<ParamMeta> = {}): ParamMeta {
  return {
    type: "float",
    units: null,
    min: null,
    max: null,
    default: null,
    description: null,
    ...overrides,
  };
}

describe("validateParamValue", () => {
  test("rejects empty input", () => {
    const result = validateParamValue("", makeMeta());
    expect(result.ok).toBe(false);
  });

  test("rejects whitespace-only input", () => {
    const result = validateParamValue("   ", makeMeta());
    expect(result.ok).toBe(false);
  });

  test("rejects non-numeric input", () => {
    const result = validateParamValue("abc", makeMeta());
    expect(result.ok).toBe(false);
  });

  test("rejects NaN/Infinity-producing input", () => {
    expect(validateParamValue("Infinity", makeMeta()).ok).toBe(false);
    expect(validateParamValue("NaN", makeMeta()).ok).toBe(false);
  });

  test("accepts a plain float with no meta bounds", () => {
    const result = validateParamValue("12.5", makeMeta());
    expect(result).toEqual({ ok: true, value: 12.5 });
  });

  test("accepts a value with no meta at all (null-safe)", () => {
    const result = validateParamValue("42", null);
    expect(result).toEqual({ ok: true, value: 42 });
  });

  test("accepts a value when meta is undefined", () => {
    const result = validateParamValue("42", undefined);
    expect(result).toEqual({ ok: true, value: 42 });
  });

  test("rejects a value below min", () => {
    const result = validateParamValue("-1", makeMeta({ min: 0 }));
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.error).toContain("0");
  });

  test("rejects a value above max", () => {
    const result = validateParamValue("21", makeMeta({ max: 20 }));
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.error).toContain("20");
  });

  test("accepts a value exactly at min", () => {
    const result = validateParamValue("0", makeMeta({ min: 0, max: 20 }));
    expect(result).toEqual({ ok: true, value: 0 });
  });

  test("accepts a value exactly at max", () => {
    const result = validateParamValue("20", makeMeta({ min: 0, max: 20 }));
    expect(result).toEqual({ ok: true, value: 20 });
  });

  test("accepts a value within min/max range", () => {
    const result = validateParamValue("12", makeMeta({ min: 0, max: 20 }));
    expect(result).toEqual({ ok: true, value: 12 });
  });

  test("rejects a fractional value for an integer type", () => {
    const result = validateParamValue("6.5", makeMeta({ type: "int32", min: 1, max: 16 }));
    expect(result.ok).toBe(false);
  });

  test("accepts a whole-number value for an integer type", () => {
    const result = validateParamValue("6", makeMeta({ type: "int32", min: 1, max: 16 }));
    expect(result).toEqual({ ok: true, value: 6 });
  });

  test("accepts negative whole numbers for signed integer types", () => {
    const result = validateParamValue("-3", makeMeta({ type: "int16" }));
    expect(result).toEqual({ ok: true, value: -3 });
  });

  test("allows fractional values for float/double types", () => {
    expect(validateParamValue("1.25", makeMeta({ type: "float" })).ok).toBe(true);
    expect(validateParamValue("1.25", makeMeta({ type: "double" })).ok).toBe(true);
  });

  test("tolerates surrounding whitespace", () => {
    const result = validateParamValue("  7  ", makeMeta({ type: "uint8", min: 0, max: 255 }));
    expect(result).toEqual({ ok: true, value: 7 });
  });

  test("checks integer-ness before range", () => {
    // Out of range AND fractional — integer-ness check should fire.
    const result = validateParamValue("99.5", makeMeta({ type: "int8", min: 0, max: 10 }));
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.error).toContain("integer");
  });
});
