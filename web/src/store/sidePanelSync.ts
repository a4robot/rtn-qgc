/**
 * Pure coupling logic for the Actions-column tab strip (SidePanel) vs.
 * `uiStore.mapMode`. Kept out of the store/component so the coupling rule
 * itself is trivially unit-testable without touching zustand or React.
 *
 * Rule: FLY and PLAN tabs mirror the map-click mode 1:1 (selecting one
 * flips the other, and vice versa — e.g. WaypointList's internal FLY/PLAN
 * toggle drives `setMapMode` directly, and the tab strip must follow it).
 * PARAMS has no map-mode equivalent, so selecting it leaves `mapMode`
 * wherever it was, and an external `mapMode` change never selects PARAMS.
 */

import type { MapMode } from "./uiStore.ts";

export type SidePanelTab = "fly" | "plan" | "params";

/**
 * The map mode that should result from selecting `tab`, given the map mode
 * currently in effect. Returns `currentMode` unchanged for the PARAMS tab.
 */
export function mapModeForTab(tab: SidePanelTab, currentMode: MapMode): MapMode {
  return tab === "params" ? currentMode : tab;
}

/**
 * The side-panel tab that should result from `mapMode` changing, e.g. from
 * WaypointList's internal toggle. MapMode has no "params" value, so this is
 * always one of "fly"/"plan" — never leaves the previous tab at "params".
 */
export function tabForMapMode(mode: MapMode): SidePanelTab {
  return mode;
}
