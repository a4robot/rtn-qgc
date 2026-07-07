import { beforeEach, expect, test } from "bun:test";

import type { Telemetry } from "../bridge/types.ts";
import { useConnectionStore, type TickMessage } from "./connectionStore.ts";
import { useVehicleStore } from "./vehicleStore.ts";

const initialConnection = useConnectionStore.getInitialState();
const initialVehicles = useVehicleStore.getInitialState();

function makeTelemetry(overrides: Partial<Telemetry> = {}): Telemetry {
  return {
    channel: "telemetry",
    seq: 1,
    timestampMs: 1_700_000_000_000,
    vehicleId: 1,
    connected: true,
    armed: false,
    flightMode: "Hold",
    position: { latitudeDeg: 13.74, longitudeDeg: 100.53, altitudeMslM: 35, altitudeRelM: 30 },
    attitude: { rollDeg: 1.5, pitchDeg: -2.0, yawDeg: 270 },
    velocity: { groundSpeedMps: 5, airSpeedMps: 6, climbRateMps: 0.2 },
    battery: { voltageV: 15.9, remainingPct: 87 },
    gps: { fixType: 3, satelliteCount: 12, hdop: 0.9 },
    ...overrides,
  };
}

function makeTick(overrides: Partial<TickMessage> = {}): TickMessage {
  return {
    channel: "tick",
    seq: 1,
    timestampMs: 1_700_000_000_000,
    channelHeads: { telemetry: 42 },
    vehicleIds: [1],
    ...overrides,
  };
}

beforeEach(() => {
  useConnectionStore.setState(initialConnection, true);
  useVehicleStore.setState(initialVehicles, true);
});

test("applyTelemetry stores the snapshot keyed by vehicleId", () => {
  useVehicleStore.getState().applyTelemetry(makeTelemetry(), 123);
  const vehicle = useVehicleStore.getState().vehicles[1];
  expect(vehicle?.flightMode).toBe("Hold");
  expect(vehicle?.battery.remainingPct).toBe(87);
  expect(vehicle?.lastUpdateAtMs).toBe(123);
  expect(vehicle).not.toHaveProperty("channel");
  expect(vehicle).not.toHaveProperty("seq");
});

test("a later snapshot replaces the vehicle entry", () => {
  const store = useVehicleStore.getState();
  store.applyTelemetry(makeTelemetry());
  store.applyTelemetry(makeTelemetry({ seq: 2, armed: true, flightMode: "Mission" }));
  const vehicle = useVehicleStore.getState().vehicles[1];
  expect(vehicle?.armed).toBe(true);
  expect(vehicle?.flightMode).toBe("Mission");
});

test("syncVehicleIds marks vanished vehicles disconnected but keeps their state", () => {
  const store = useVehicleStore.getState();
  store.applyTelemetry(makeTelemetry({ vehicleId: 1 }));
  store.applyTelemetry(makeTelemetry({ vehicleId: 2 }));
  store.syncVehicleIds([2]);
  const vehicles = useVehicleStore.getState().vehicles;
  expect(vehicles[1]?.connected).toBe(false);
  expect(vehicles[1]?.flightMode).toBe("Hold");
  expect(vehicles[2]?.connected).toBe(true);
});

test("applyTick records tick time, vehicle ids, and clock offset", () => {
  const receivedAtMs = 1_700_000_000_050;
  useConnectionStore
    .getState()
    .applyTick(makeTick({ serverTimeUs: 1_700_000_000_000_000 }), receivedAtMs);
  const state = useConnectionStore.getState();
  expect(state.lastTickAtMs).toBe(receivedAtMs);
  expect(state.lastTickServerTimeUs).toBe(1_700_000_000_000_000);
  expect(state.clockOffsetMs).toBe(-50); // first tick: no smoothing yet
  expect(state.vehicleIds).toEqual([1]);
});

test("applyTick falls back to timestampMs when serverTimeUs is absent", () => {
  useConnectionStore.getState().applyTick(makeTick(), 1_700_000_000_000);
  expect(useConnectionStore.getState().lastTickServerTimeUs).toBe(1_700_000_000_000_000);
});

test("clock offset is smoothed across ticks", () => {
  const store = useConnectionStore.getState();
  store.applyTick(makeTick({ serverTimeUs: 1_700_000_000_000_000 }), 1_700_000_000_000);
  store.applyTick(makeTick({ seq: 2, serverTimeUs: 1_700_000_001_000_000 }), 1_700_000_001_100);
  // First offset 0, second raw offset -100, EMA(0.2): 0 + 0.2 * (-100) = -20.
  expect(useConnectionStore.getState().clockOffsetMs).toBeCloseTo(-20);
});

test("setState counts reconnect attempts and resets them on connect", () => {
  const store = useConnectionStore.getState();
  store.setState("connecting");
  store.setState("reconnecting");
  store.setState("reconnecting");
  expect(useConnectionStore.getState().reconnectAttempts).toBe(2);
  store.setState("connected");
  expect(useConnectionStore.getState().reconnectAttempts).toBe(0);
  expect(useConnectionStore.getState().state).toBe("connected");
});
