import { expect, test } from "bun:test";

import { planVehicleSwitch } from "./session.ts";

test("switching to a different vehicle while connected resubscribes immediately", () => {
  expect(planVehicleSwitch(1, 2, "connected")).toEqual({ changed: true, resubscribeNow: true });
});

test("switching to a different vehicle while disconnected defers to the next handshake", () => {
  expect(planVehicleSwitch(1, 2, "disconnected")).toEqual({ changed: true, resubscribeNow: false });
});

test("switching to a different vehicle while reconnecting defers to the next handshake", () => {
  expect(planVehicleSwitch(1, 2, "reconnecting")).toEqual({ changed: true, resubscribeNow: false });
});

test("selecting the already-active vehicle is a no-op regardless of connection state", () => {
  expect(planVehicleSwitch(1, 1, "connected")).toEqual({ changed: false, resubscribeNow: false });
  expect(planVehicleSwitch(1, 1, "disconnected")).toEqual({ changed: false, resubscribeNow: false });
});
