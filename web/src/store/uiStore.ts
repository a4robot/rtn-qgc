/**
 * UI store — view-local state that isn't derived from bridge telemetry
 * (contrast connectionStore/vehicleStore, which mirror bridge state).
 * Currently just the active vehicle for multi-vehicle selection.
 *
 * Dependency direction: this module has zero knowledge of BridgeClient or
 * bridge/session.ts — it only holds `activeVehicleId` and a setter, same as
 * any other zustand store. The side effect of actually re-subscribing
 * telemetry for the new vehicle lives on the session handle
 * (bridge/session.ts's `BridgeSessionHandle.setVehicle`), which the orchestrator
 * (App.tsx) owns alongside the BridgeClient.
 *
 * `useVehicleSwitcher` is the one place those two are wired together: it
 * takes the session handle as a parameter (dependency injected by the
 * caller, not imported), so the store stays session-agnostic and the
 * session stays store-agnostic — neither imports the other. Presentation
 * components (VehicleSelect) then only see a plain `onSelect(id)` callback
 * prop; they never import the store or the session directly.
 */

import { create } from "zustand";

export interface UiStoreState {
  /** Vehicle currently shown across telemetry/map/actions panels. */
  activeVehicleId: number;
  setActiveVehicleId: (id: number) => void;
}

export const useUiStore = create<UiStoreState>()((set) => ({
  // Default is a placeholder; App.tsx seeds the real initial value (e.g. from
  // the `?vehicle=` URL override) once on mount.
  activeVehicleId: 1,
  setActiveVehicleId: (id) => set({ activeVehicleId: id }),
}));

/** The currently active vehicle id. */
export function useActiveVehicle(): number {
  return useUiStore((state) => state.activeVehicleId);
}

/** Minimal shape `useVehicleSwitcher` needs — matches `BridgeSessionHandle`. */
export interface VehicleSwitchTarget {
  setVehicle(id: number): void;
}

/**
 * Wiring helper: returns an `onSelect(id)` callback that updates
 * `activeVehicleId` in this store and drives the session's telemetry
 * resubscribe, in one place. Pass `null` (e.g. before the session is up) to
 * get a callback that only updates the store.
 */
export function useVehicleSwitcher(sessionHandle: VehicleSwitchTarget | null): (id: number) => void {
  const setActiveVehicleId = useUiStore((state) => state.setActiveVehicleId);
  return (id: number) => {
    setActiveVehicleId(id);
    sessionHandle?.setVehicle(id);
  };
}
