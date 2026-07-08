/**
 * Fixture-mode basics: the recorded-style survey fixtures parse and match
 * PROTOCOL.md §4/§7.1's schemas, and the mock server's opt-in fixture mode
 * (`MOCK_FIXTURE=1`) actually replays them over the wire.
 *
 * Run: bun test mock/fixtures/survey.test.ts   (part of `bun test`)
 */

import { describe, expect, test } from "bun:test";

import telemetrySurvey from "./telemetry-survey.json";
import missionSurvey from "./mission-survey.json";

// --- static fixture shape -----------------------------------------------

describe("fixtures/telemetry-survey.json", () => {
  test("parses with the recorded-style shape (description, rateHz, samples)", () => {
    expect(typeof telemetrySurvey.description).toBe("string");
    expect(telemetrySurvey.rateHz).toBe(1);
    expect(Array.isArray(telemetrySurvey.samples)).toBe(true);
    expect(telemetrySurvey.samples.length).toBeGreaterThanOrEqual(100);
  });

  test("every sample is a plain §4 leaf set (no channel/seq/timestampMs envelope)", () => {
    for (const sample of telemetrySurvey.samples) {
      const s = sample as Record<string, unknown>;
      expect(s.type).toBeUndefined();
      expect(s.channel).toBeUndefined();
      expect(s.seq).toBeUndefined();
      expect(s.timeUs).toBeUndefined();

      const attitude = s.attitude as Record<string, unknown>;
      const position = s.position as Record<string, unknown>;
      const velocity = s.velocity as Record<string, unknown>;
      const battery = s.battery as Record<string, unknown>;
      const gps = s.gps as Record<string, unknown>;
      expect(typeof attitude.roll).toBe("number");
      expect(typeof attitude.pitch).toBe("number");
      expect(typeof attitude.yaw).toBe("number");
      expect(typeof position.lat).toBe("number");
      expect(typeof position.lon).toBe("number");
      expect(typeof position.altMSL).toBe("number");
      expect(typeof position.altRel).toBe("number");
      expect(typeof velocity.groundSpeed).toBe("number");
      expect(velocity.airSpeed === null || typeof velocity.airSpeed === "number").toBe(true);
      expect(typeof velocity.climbRate).toBe("number");
      expect(typeof battery.percent).toBe("number");
      expect(typeof battery.voltage).toBe("number");
      expect(typeof battery.current).toBe("number");
      expect(typeof gps.fix).toBe("string");
      expect(typeof gps.count).toBe("number");
      expect(typeof gps.hdop).toBe("number");
      expect(typeof s.flightMode).toBe("string");
      expect(typeof s.armed).toBe("boolean");
    }
  });

  test("flight near Zurich, GPS 3d throughout with count 12-17", () => {
    for (const sample of telemetrySurvey.samples) {
      const s = sample as Record<string, unknown>;
      const position = s.position as { lat: number; lon: number };
      const gps = s.gps as { fix: string; count: number };
      expect(position.lat).toBeGreaterThan(47.3);
      expect(position.lat).toBeLessThan(47.5);
      expect(position.lon).toBeGreaterThan(8.4);
      expect(position.lon).toBeLessThan(8.6);
      expect(gps.fix).toBe("3d");
      expect(gps.count).toBeGreaterThanOrEqual(12);
      expect(gps.count).toBeLessThanOrEqual(17);
    }
  });

  test("battery drains from 100% to ~82%", () => {
    const first = telemetrySurvey.samples[0]!.battery.percent;
    const last = telemetrySurvey.samples[telemetrySurvey.samples.length - 1]!.battery.percent;
    expect(first).toBe(100);
    expect(last).toBeGreaterThan(80);
    expect(last).toBeLessThan(84);
  });

  test("climbs to ~50 m relative altitude and returns to the ground", () => {
    const altitudes = telemetrySurvey.samples.map((s) => s.position.altRel);
    expect(Math.max(...altitudes)).toBeCloseTo(50, 0);
    expect(altitudes[0]).toBe(0);
    expect(altitudes[altitudes.length - 1]).toBe(0);
  });

  test("flight includes at least one null airSpeed sample (§4 nullable discipline)", () => {
    expect(telemetrySurvey.samples.some((s) => s.velocity.airSpeed === null)).toBe(true);
    expect(telemetrySurvey.samples.some((s) => s.velocity.airSpeed !== null)).toBe(true);
  });
});

describe("fixtures/mission-survey.json", () => {
  test("6-item survey mission: takeoff, 4 waypoints, RTL (§7.1)", () => {
    expect(missionSurvey.items.length).toBe(6);
    missionSurvey.items.forEach((item, i) => {
      expect(item.seq).toBe(i);
      expect(item.frame).toBe(6); // MAV_FRAME_GLOBAL_RELATIVE_ALT_INT
      expect(typeof item.autoContinue).toBe("boolean");
      expect(typeof item.lat).toBe("number");
      expect(typeof item.lon).toBe("number");
      expect(typeof item.alt).toBe("number");
    });
    expect(missionSurvey.items[0]!.command).toBe(22); // NAV_TAKEOFF
    expect(missionSurvey.items[0]!.current).toBe(true);
    for (let i = 1; i <= 4; i++) {
      expect(missionSurvey.items[i]!.command).toBe(16); // NAV_WAYPOINT
      expect(missionSurvey.items[i]!.current).toBe(false);
    }
    expect(missionSurvey.items[5]!.command).toBe(20); // NAV_RETURN_TO_LAUNCH
  });
});

// --- live replay over the wire --------------------------------------------
//
// Run in a child process (mock/fixtures/verify-replay.ts) rather than inline
// here: src/bridge/BridgeClient.test.ts monkeypatches `globalThis.WebSocket`
// with a fake in a `beforeEach` and never restores it, which — since `bun
// test` runs all files in one shared process — leaks into any in-process
// WebSocket use by a test file that runs afterward. Spawning gives the
// checker script a clean process with the real WebSocket global.

test(
  "mock server MOCK_FIXTURE=1 replay produces §4-shaped telemetry + the 6-item survey mission",
  async () => {
    const proc = Bun.spawn(["bun", new URL("./verify-replay.ts", import.meta.url).pathname], {
      env: { ...process.env, MOCK_PORT: "8879" },
      stdout: "pipe",
      stderr: "inherit",
    });
    const stdout = await new Response(proc.stdout).text();
    const exitCode = await proc.exited;

    for (const line of stdout.split("\n").filter((l) => l.startsWith("RESULT: "))) {
      console.log(line);
    }
    expect(exitCode, stdout).toBe(0);
  },
  15000,
);
