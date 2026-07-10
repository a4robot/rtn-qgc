import { describe, expect, test } from "bun:test";

import { validateLinkFields, type LinkFieldsInput } from "./validateLinkFields.ts";

function makeInput(overrides: Partial<LinkFieldsInput> = {}): LinkFieldsInput {
  return {
    linkType: "udp",
    name: "Telemetry",
    listenPort: "14550",
    ...overrides,
  };
}

describe("validateLinkFields — name", () => {
  test("rejects an empty name", () => {
    expect(validateLinkFields(makeInput({ name: "" })).ok).toBe(false);
  });

  test("rejects a whitespace-only name", () => {
    expect(validateLinkFields(makeInput({ name: "   " })).ok).toBe(false);
  });

  test("trims the name", () => {
    const result = validateLinkFields(makeInput({ name: "  Telemetry  " }));
    expect(result.ok && result.value.name).toBe("Telemetry");
  });
});

describe("validateLinkFields — serial", () => {
  test("accepts a valid serial link", () => {
    const result = validateLinkFields(
      makeInput({ linkType: "serial", path: "/dev/ttyUSB0", baud: "57600" }),
    );
    expect(result).toEqual({
      ok: true,
      value: { type: "serial", name: "Telemetry", port: "/dev/ttyUSB0", baud: 57600 },
    });
  });

  test("rejects a missing path", () => {
    const result = validateLinkFields(makeInput({ linkType: "serial", path: "", baud: "57600" }));
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.error).toContain("path");
  });

  test("rejects a non-numeric baud", () => {
    const result = validateLinkFields(makeInput({ linkType: "serial", path: "/dev/ttyUSB0", baud: "fast" }));
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.error).toContain("Baud");
  });

  test("rejects a zero/negative baud", () => {
    expect(validateLinkFields(makeInput({ linkType: "serial", path: "/dev/ttyUSB0", baud: "0" })).ok).toBe(false);
    expect(validateLinkFields(makeInput({ linkType: "serial", path: "/dev/ttyUSB0", baud: "-1" })).ok).toBe(false);
  });

  test("rejects a fractional baud", () => {
    expect(validateLinkFields(makeInput({ linkType: "serial", path: "/dev/ttyUSB0", baud: "57600.5" })).ok).toBe(
      false,
    );
  });
});

describe("validateLinkFields — udp", () => {
  test("accepts a valid udp link", () => {
    const result = validateLinkFields(makeInput({ linkType: "udp", listenPort: "14550" }));
    expect(result).toEqual({ ok: true, value: { type: "udp", name: "Telemetry", listenPort: 14550 } });
  });

  test("rejects a missing listenPort", () => {
    const result = validateLinkFields(makeInput({ linkType: "udp", listenPort: "" }));
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.error).toContain("port");
  });

  test("rejects an out-of-range listenPort", () => {
    expect(validateLinkFields(makeInput({ linkType: "udp", listenPort: "0" })).ok).toBe(false);
    expect(validateLinkFields(makeInput({ linkType: "udp", listenPort: "70000" })).ok).toBe(false);
  });

  test("rejects a non-integer listenPort", () => {
    expect(validateLinkFields(makeInput({ linkType: "udp", listenPort: "14550.5" })).ok).toBe(false);
  });
});

describe("validateLinkFields — tcp", () => {
  test("accepts a valid tcp link", () => {
    const result = validateLinkFields(makeInput({ linkType: "tcp", host: "192.168.1.10", port: "5760" }));
    expect(result).toEqual({
      ok: true,
      value: { type: "tcp", name: "Telemetry", host: "192.168.1.10", port: 5760 },
    });
  });

  test("rejects a missing host", () => {
    const result = validateLinkFields(makeInput({ linkType: "tcp", host: "", port: "5760" }));
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.error).toContain("Host");
  });

  test("rejects an invalid port even with a valid host", () => {
    expect(validateLinkFields(makeInput({ linkType: "tcp", host: "127.0.0.1", port: "abc" })).ok).toBe(false);
  });
});
