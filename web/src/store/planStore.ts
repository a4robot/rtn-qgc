/**
 * Plan store — client-side DRAFT mission being edited, PROTOCOL.md §7.
 *
 * Distinct from missionStore (the on-vehicle mission, read-only state mirror
 * of the bridge's `mission` channel): this store holds a *local* item list
 * the user is composing, which only reaches the vehicle when WaypointList
 * explicitly sends a `missionUpload` (§7.2). `loadFrom` seeds the draft from
 * the current on-vehicle mission (missionStore) as a starting point to edit.
 *
 * Invariant: `seq === array index` for every item, always — the bridge's
 * MissionChannel rejects an upload whose `items[i].seq !== i` (mission items
 * are positional, matching PlanManager's list-index addressing, not a free
 * "seq" field), so every mutation here re-derives `seq` from position rather
 * than trusting a caller-supplied value.
 */

import { create } from "zustand";

import type { MissionItem } from "../bridge/types.ts";

/** `MAV_FRAME` for new waypoints, PROTOCOL.md §7.1: 6 = GLOBAL_RELATIVE_ALT_INT. */
const DEFAULT_FRAME = 6;
/** `MAV_CMD` for new waypoints, PROTOCOL.md §7.1: 16 = NAV_WAYPOINT. */
const DEFAULT_COMMAND = 16;
/** Default relative altitude (meters) for a newly added waypoint. */
const DEFAULT_ALT_M = 50;

/** Re-derive `seq` from array position for every item — the invariant this store enforces. */
function reindex(items: MissionItem[]): MissionItem[] {
  return items.map((item, i) => (item.seq === i ? item : { ...item, seq: i }));
}

export interface PlanStoreState {
  /** Draft mission items being edited, always satisfying seq === index. */
  items: MissionItem[];

  /** Append a new NAV_WAYPOINT at the clicked position, default altitude. */
  addWaypoint: (lat: number, lon: number, alt?: number) => void;
  /** Remove the item at `seq` and close the gap, re-deriving seq for the rest. */
  removeItem: (seq: number) => void;
  /** Edit one item's altitude in place. */
  updateItemAlt: (seq: number, alt: number) => void;
  /** Drop the entire draft. */
  clear: () => void;
  /** Replace the draft wholesale (e.g. seeded from the on-vehicle mission). */
  loadFrom: (items: MissionItem[]) => void;
}

export const usePlanStore = create<PlanStoreState>()((set) => ({
  items: [],

  addWaypoint: (lat, lon, alt = DEFAULT_ALT_M) =>
    set((prev) => ({
      items: [
        ...prev.items,
        {
          seq: prev.items.length,
          frame: DEFAULT_FRAME,
          command: DEFAULT_COMMAND,
          current: false,
          autoContinue: true,
          param1: 0,
          param2: 0,
          param3: 0,
          param4: 0,
          lat,
          lon,
          alt,
        },
      ],
    })),

  removeItem: (seq) =>
    set((prev) => ({
      items: reindex(prev.items.filter((item) => item.seq !== seq)),
    })),

  updateItemAlt: (seq, alt) =>
    set((prev) => ({
      items: reindex(prev.items.map((item) => (item.seq === seq ? { ...item, alt } : item))),
    })),

  clear: () => set({ items: [] }),

  loadFrom: (items) => set({ items: reindex(items) }),
}));

/** The current draft mission items. */
export function usePlanItems(): MissionItem[] {
  return usePlanStore((state) => state.items);
}
