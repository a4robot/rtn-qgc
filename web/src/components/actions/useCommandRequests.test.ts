import { expect, test } from "bun:test";

import { commandRequestsReducer, latestRequestForAction, type CommandRequestsState } from "./useCommandRequests.ts";

test("sent adds a pending request keyed by id", () => {
  const state = commandRequestsReducer({}, { type: "sent", id: "cmd-0-arm", action: "arm", vehicleId: 1, atMs: 100 });
  expect(state["cmd-0-arm"]).toEqual({
    action: "arm",
    vehicleId: 1,
    phase: "pending",
    sentAtMs: 100,
  });
});

test("ack transitions a tracked request to accepted", () => {
  const sent = commandRequestsReducer(
    {},
    { type: "sent", id: "cmd-0-arm", action: "arm", vehicleId: 1, atMs: 100 },
  );
  const acked = commandRequestsReducer(sent, { type: "ack", id: "cmd-0-arm", status: "accepted" });
  expect(acked["cmd-0-arm"]?.phase).toBe("accepted");
});

test("ack carries the rejection reason", () => {
  const sent = commandRequestsReducer(
    {},
    { type: "sent", id: "cmd-1-takeoff", action: "takeoff", vehicleId: 1, atMs: 100 },
  );
  const acked = commandRequestsReducer(sent, {
    type: "ack",
    id: "cmd-1-takeoff",
    status: "rejected",
    reason: "Vehicle not armable: GPS fix required",
  });
  expect(acked["cmd-1-takeoff"]).toMatchObject({
    phase: "rejected",
    reason: "Vehicle not armable: GPS fix required",
  });
});

test("ack for an untracked id is a no-op (same reference)", () => {
  const state: CommandRequestsState = {};
  const next = commandRequestsReducer(state, { type: "ack", id: "unknown", status: "accepted" });
  expect(next).toBe(state);
});

test("progress updates progress/message on a tracked request without changing phase", () => {
  const sent = commandRequestsReducer(
    {},
    { type: "sent", id: "cmd-2-rtl", action: "rtl", vehicleId: 1, atMs: 100 },
  );
  const progressed = commandRequestsReducer(sent, {
    type: "progress",
    id: "cmd-2-rtl",
    progress: 0.4,
    message: "Returning",
  });
  expect(progressed["cmd-2-rtl"]).toMatchObject({
    phase: "pending",
    progress: 0.4,
    message: "Returning",
  });
});

test("progress for an untracked id is a no-op (same reference)", () => {
  const state: CommandRequestsState = {};
  const next = commandRequestsReducer(state, { type: "progress", id: "unknown", progress: 0.5 });
  expect(next).toBe(state);
});

test("clear removes the request", () => {
  const sent = commandRequestsReducer(
    {},
    { type: "sent", id: "cmd-3-land", action: "land", vehicleId: 1, atMs: 100 },
  );
  const cleared = commandRequestsReducer(sent, { type: "clear", id: "cmd-3-land" });
  expect(cleared).not.toHaveProperty("cmd-3-land");
  expect(Object.keys(cleared)).toHaveLength(0);
});

test("clear on an id not present is a no-op (same reference)", () => {
  const state: CommandRequestsState = {};
  const next = commandRequestsReducer(state, { type: "clear", id: "unknown" });
  expect(next).toBe(state);
});

test("clear leaves other tracked requests untouched", () => {
  let state = commandRequestsReducer(
    {},
    { type: "sent", id: "cmd-4-arm", action: "arm", vehicleId: 1, atMs: 100 },
  );
  state = commandRequestsReducer(state, {
    type: "sent",
    id: "cmd-5-takeoff",
    action: "takeoff",
    vehicleId: 1,
    atMs: 101,
  });
  const cleared = commandRequestsReducer(state, { type: "clear", id: "cmd-4-arm" });
  expect(cleared["cmd-4-arm"]).toBeUndefined();
  expect(cleared["cmd-5-takeoff"]?.action).toBe("takeoff");
});

test("latestRequestForAction returns the most recently sent match", () => {
  let state = commandRequestsReducer(
    {},
    { type: "sent", id: "cmd-0-pause", action: "pause", vehicleId: 1, atMs: 100 },
  );
  state = commandRequestsReducer(state, {
    type: "sent",
    id: "cmd-1-pause",
    action: "pause",
    vehicleId: 1,
    atMs: 200,
  });
  const latest = latestRequestForAction(state, "pause");
  expect(latest?.sentAtMs).toBe(200);
});

test("latestRequestForAction returns undefined when no request matches", () => {
  const state = commandRequestsReducer(
    {},
    { type: "sent", id: "cmd-0-arm", action: "arm", vehicleId: 1, atMs: 100 },
  );
  expect(latestRequestForAction(state, "land")).toBeUndefined();
});
