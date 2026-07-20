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
 *
 * `sidePanelTab` (SidePanel's FLY/PLAN/PARAMS/SETTINGS tab strip) is coupled to
 * `mapMode` two ways — selecting FLY/PLAN also flips `mapMode`, and an
 * external `mapMode` change (e.g. WaypointList's own FLY/PLAN toggle) syncs
 * the tab back — via the pure helpers in `sidePanelSync.ts`, so the actual
 * "what should the other one become" rule is unit-tested without zustand.
 */

import { create } from "zustand";

import { mapModeForTab, tabForMapMode, type SidePanelTab } from "./sidePanelSync.ts";

/** Map interaction mode: "fly" drives GotoOnClick, "plan" drives WaypointAdder. */
export type MapMode = "fly" | "plan";

export type { SidePanelTab } from "./sidePanelSync.ts";

export interface UiStoreState {
  /** Vehicle currently shown across telemetry/map/actions panels. */
  activeVehicleId: number;
  setActiveVehicleId: (id: number) => void;
  /** Which map-click behavior is active. Default "fly" (click-to-goto). */
  mapMode: MapMode;
  setMapMode: (mode: MapMode) => void;
  /** Which SidePanel tab (FLY/PLAN/PARAMS/SETTINGS) is showing. Default "fly". */
  sidePanelTab: SidePanelTab;
  setSidePanelTab: (tab: SidePanelTab) => void;
  /** Vehicle marker style: "default" (red/blue) or "high-vis" (green/gold, 1.5x scale) */
  vehicleMarkerStyle: "default" | "high-vis";
  setVehicleMarkerStyle: (style: "default" | "high-vis") => void;
  /** Map base layer style: "street" (Carto Dark) or "satellite" (Esri) */
  mapStyle: "street" | "satellite";
  setMapStyle: (style: "street" | "satellite") => void;
}

export const useUiStore = create<UiStoreState>()((set, get) => ({
  // Default is a placeholder; App.tsx seeds the real initial value (e.g. from
  // the `?vehicle=` URL override) once on mount.
  activeVehicleId: 1,
  setActiveVehicleId: (id) => set({ activeVehicleId: id }),
  mapMode: "fly",
  setMapMode: (mode) => set({ mapMode: mode, sidePanelTab: tabForMapMode(mode) }),
  sidePanelTab: "fly",
  setSidePanelTab: (tab) => set({ sidePanelTab: tab, mapMode: mapModeForTab(tab, get().mapMode) }),
  vehicleMarkerStyle: (localStorage.getItem("vehicleMarkerStyle") as "default" | "high-vis") || "high-vis",
  setVehicleMarkerStyle: (style) => {
    localStorage.setItem("vehicleMarkerStyle", style);
    set({ vehicleMarkerStyle: style });
  },
  mapStyle: (localStorage.getItem("mapStyle") as "street" | "satellite") || "street",
  setMapStyle: (style) => {
    localStorage.setItem("mapStyle", style);
    set({ mapStyle: style });
  },
}));

/** Current map interaction mode. */
export function useMapMode(): MapMode {
  return useUiStore((state) => state.mapMode);
}

/** Current SidePanel tab. */
export function useSidePanelTab(): SidePanelTab {
  return useUiStore((state) => state.sidePanelTab);
}

/** The currently active vehicle id. */
export function useActiveVehicle(): number {
  return useUiStore((state) => state.activeVehicleId);
}

/** The current vehicle marker style. */
export function useVehicleMarkerStyle(): "default" | "high-vis" {
  return useUiStore((state) => state.vehicleMarkerStyle);
}

/** The current map base layer style. */
export function useMapStyle(): "street" | "satellite" {
  return useUiStore((state) => state.mapStyle);
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
