/**
 * Mission store — latest mission state per vehicle, keyed by vehicleId.
 *
 * State-not-events, same as vehicleStore: every missionState message is a
 * full snapshot/update for one vehicle's mission (current item list plus the
 * seq the vehicle is currently flying to), so applying one simply replaces
 * that vehicle's entry.
 */

import { create } from "zustand";

import type { MissionItem, MissionState } from "../bridge/types.ts";

/** Renderable per-vehicle mission state: the payload minus the channel envelope. */
export interface MissionRecord {
  currentSeq: number;
  items: MissionItem[];
  /** Local receipt time of the last missionState message, ms since epoch. */
  lastUpdateAtMs: number;
}

export interface MissionStoreState {
  /** Latest mission per vehicle, keyed by MAVLink system id. */
  missions: Record<number, MissionRecord>;

  applyMissionState: (mission: MissionState, receivedAtMs?: number) => void;
  /** Drop all mission state (e.g. on disconnect if a hard reset is wanted). */
  clear: () => void;
}

export const useMissionStore = create<MissionStoreState>()((set) => ({
  missions: {},

  applyMissionState: (mission, receivedAtMs = Date.now()) =>
    set((prev) => ({
      missions: {
        ...prev.missions,
        [mission.vehicleId]: {
          currentSeq: mission.currentSeq,
          items: mission.items,
          lastUpdateAtMs: receivedAtMs,
        },
      },
    })),

  clear: () => set({ missions: {} }),
}));

/** Latest mission for one vehicle, or undefined if never seen. */
export function useMission(vehicleId: number): MissionRecord | undefined {
  return useMissionStore((state) => state.missions[vehicleId]);
}

/** All known missions keyed by vehicleId. */
export function useMissions(): Record<number, MissionRecord> {
  return useMissionStore((state) => state.missions);
}
