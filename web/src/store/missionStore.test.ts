import { beforeEach, expect, test } from "bun:test";

import type { MissionItem, MissionState } from "../bridge/types.ts";
import { useMissionStore } from "./missionStore.ts";

const initialMissions = useMissionStore.getInitialState();

function makeItem(overrides: Partial<MissionItem> = {}): MissionItem {
  return {
    seq: 0,
    frame: 6,
    command: 16,
    current: true,
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

function makeMissionState(overrides: Partial<MissionState> = {}): MissionState {
  return {
    channel: "mission",
    seq: 1,
    snapshot: true,
    vehicleId: 1,
    currentSeq: 0,
    items: [makeItem()],
    ...overrides,
  } as MissionState;
}

beforeEach(() => {
  useMissionStore.setState(initialMissions, true);
});

test("applyMissionState stores the mission keyed by vehicleId", () => {
  useMissionStore.getState().applyMissionState(makeMissionState(), 123);
  const mission = useMissionStore.getState().missions[1];
  expect(mission?.currentSeq).toBe(0);
  expect(mission?.items).toHaveLength(1);
  expect(mission?.lastUpdateAtMs).toBe(123);
});

test("a later update replaces the mission entry (currentSeq advance)", () => {
  const store = useMissionStore.getState();
  store.applyMissionState(makeMissionState());
  store.applyMissionState(
    makeMissionState({
      seq: 2,
      currentSeq: 1,
      items: [makeItem({ current: false }), makeItem({ seq: 1, current: true, lat: 13.74 })],
    }),
  );
  const mission = useMissionStore.getState().missions[1];
  expect(mission?.currentSeq).toBe(1);
  expect(mission?.items).toHaveLength(2);
});

test("an empty item list (post-clear) is stored as-is", () => {
  const store = useMissionStore.getState();
  store.applyMissionState(makeMissionState());
  store.applyMissionState(makeMissionState({ seq: 2, currentSeq: 0, items: [] }));
  const mission = useMissionStore.getState().missions[1];
  expect(mission?.items).toEqual([]);
});

test("missions for different vehicles don't clobber each other", () => {
  const store = useMissionStore.getState();
  store.applyMissionState(makeMissionState({ vehicleId: 1 }));
  store.applyMissionState(makeMissionState({ vehicleId: 2, currentSeq: 3 }));
  const missions = useMissionStore.getState().missions;
  expect(missions[1]?.currentSeq).toBe(0);
  expect(missions[2]?.currentSeq).toBe(3);
});

test("clear drops all mission state", () => {
  const store = useMissionStore.getState();
  store.applyMissionState(makeMissionState());
  store.clear();
  expect(useMissionStore.getState().missions).toEqual({});
});
