import { beforeEach, expect, test } from "bun:test";

import type { MissionItem } from "../bridge/types.ts";
import { usePlanStore } from "./planStore.ts";

const initialPlan = usePlanStore.getInitialState();

function makeItem(overrides: Partial<MissionItem> = {}): MissionItem {
  return {
    seq: 0,
    frame: 6,
    command: 16,
    current: false,
    autoContinue: true,
    param1: 0,
    param2: 0,
    param3: 0,
    param4: 0,
    lat: 13.7367,
    lon: 100.5232,
    alt: 30,
    ...overrides,
  };
}

beforeEach(() => {
  usePlanStore.setState(initialPlan, true);
});

test("addWaypoint appends a NAV_WAYPOINT item at seq === array index, default alt", () => {
  const store = usePlanStore.getState();
  store.addWaypoint(13.74, 100.53);
  store.addWaypoint(13.75, 100.54);

  const items = usePlanStore.getState().items;
  expect(items).toHaveLength(2);
  expect(items[0]).toMatchObject({ seq: 0, lat: 13.74, lon: 100.53, alt: 50, frame: 6, command: 16, autoContinue: true });
  expect(items[1]).toMatchObject({ seq: 1, lat: 13.75, lon: 100.54, alt: 50 });
});

test("addWaypoint accepts an explicit altitude override", () => {
  usePlanStore.getState().addWaypoint(13.74, 100.53, 75);
  expect(usePlanStore.getState().items[0]?.alt).toBe(75);
});

test("removeItem closes the gap and re-derives seq for the remaining items", () => {
  const store = usePlanStore.getState();
  store.addWaypoint(13.74, 100.53);
  store.addWaypoint(13.75, 100.54);
  store.addWaypoint(13.76, 100.55);

  store.removeItem(1); // remove the middle item

  const items = usePlanStore.getState().items;
  expect(items).toHaveLength(2);
  expect(items[0]).toMatchObject({ seq: 0, lat: 13.74 });
  expect(items[1]).toMatchObject({ seq: 1, lat: 13.76 }); // was seq 2, re-indexed to 1
});

test("removeItem on an unknown seq is a no-op", () => {
  const store = usePlanStore.getState();
  store.addWaypoint(13.74, 100.53);
  store.removeItem(99);
  expect(usePlanStore.getState().items).toHaveLength(1);
});

test("updateItemAlt edits only the matching item's altitude", () => {
  const store = usePlanStore.getState();
  store.addWaypoint(13.74, 100.53);
  store.addWaypoint(13.75, 100.54);

  store.updateItemAlt(1, 120);

  const items = usePlanStore.getState().items;
  expect(items[0]?.alt).toBe(50);
  expect(items[1]?.alt).toBe(120);
  expect(items[1]?.seq).toBe(1); // seq invariant untouched
});

test("clear drops all draft items", () => {
  const store = usePlanStore.getState();
  store.addWaypoint(13.74, 100.53);
  store.clear();
  expect(usePlanStore.getState().items).toEqual([]);
});

test("loadFrom replaces the draft wholesale, re-deriving seq from position", () => {
  const store = usePlanStore.getState();
  store.addWaypoint(13.74, 100.53); // pre-existing draft item, should be replaced entirely

  store.loadFrom([
    makeItem({ seq: 0, lat: 13.7367, lon: 100.5232, alt: 20, command: 22 }), // takeoff
    makeItem({ seq: 1, lat: 13.74, lon: 100.53, alt: 30 }),
  ]);

  const items = usePlanStore.getState().items;
  expect(items).toHaveLength(2);
  expect(items[0]).toMatchObject({ seq: 0, command: 22, alt: 20 });
  expect(items[1]).toMatchObject({ seq: 1, alt: 30 });
});

test("loadFrom re-indexes seq even if the source list has non-contiguous or out-of-order seq", () => {
  usePlanStore.getState().loadFrom([makeItem({ seq: 5 }), makeItem({ seq: 9, lat: 14.0 })]);

  const items = usePlanStore.getState().items;
  expect(items[0]?.seq).toBe(0);
  expect(items[1]?.seq).toBe(1);
  expect(items[1]?.lat).toBe(14.0);
});

test("loadFrom with an empty list clears the draft", () => {
  const store = usePlanStore.getState();
  store.addWaypoint(13.74, 100.53);
  store.loadFrom([]);
  expect(usePlanStore.getState().items).toEqual([]);
});
