/**
 * Vehicle store — latest telemetry state per vehicle, keyed by vehicleId.
 *
 * State-not-events: every telemetry message is a full snapshot for one
 * vehicle, so applying one simply replaces that vehicle's entry. Vehicles
 * that disappear from the tick's `vehicleIds` are marked disconnected (their
 * last-known state is kept greyed-out rather than dropped, per §11.1).
 */

import { create } from "zustand";

import type { Telemetry } from "../bridge/types.ts";

/**
 * Renderable per-vehicle state: the telemetry snapshot minus the channel
 * envelope (`channel`/`seq`), plus bookkeeping for staleness display.
 */
export type VehicleState = Omit<Telemetry, "channel" | "seq"> & {
  /** Local receipt time of the last telemetry snapshot, ms since epoch. */
  lastUpdateAtMs: number;
};

export interface VehicleStoreState {
  /** Latest state per vehicle, keyed by MAVLink system id. */
  vehicles: Record<number, VehicleState>;

  applyTelemetry: (telemetry: Telemetry, receivedAtMs?: number) => void;
  /** Reconcile with the bridge's vehicle list (from tick): mark missing vehicles disconnected. */
  syncVehicleIds: (vehicleIds: number[]) => void;
  /** Drop all vehicle state (e.g. on disconnect if a hard reset is wanted). */
  clear: () => void;
}

export const useVehicleStore = create<VehicleStoreState>()((set) => ({
  vehicles: {},

  applyTelemetry: (telemetry, receivedAtMs = Date.now()) =>
    set((prev) => {
      const { channel: _channel, seq: _seq, ...state } = telemetry;
      return {
        vehicles: {
          ...prev.vehicles,
          [telemetry.vehicleId]: { ...state, lastUpdateAtMs: receivedAtMs },
        },
      };
    }),

  syncVehicleIds: (vehicleIds) =>
    set((prev) => {
      const known = new Set(vehicleIds);
      let changed = false;
      const vehicles: Record<number, VehicleState> = { ...prev.vehicles };
      for (const [key, vehicle] of Object.entries(vehicles)) {
        if (vehicle.connected && !known.has(vehicle.vehicleId)) {
          vehicles[Number(key)] = { ...vehicle, connected: false };
          changed = true;
        }
      }
      return changed ? { vehicles } : prev;
    }),

  clear: () => set({ vehicles: {} }),
}));

/** Latest state for one vehicle, or undefined if never seen. */
export function useVehicle(vehicleId: number): VehicleState | undefined {
  return useVehicleStore((state) => state.vehicles[vehicleId]);
}

/** All known vehicle states keyed by vehicleId. */
export function useVehicles(): Record<number, VehicleState> {
  return useVehicleStore((state) => state.vehicles);
}
