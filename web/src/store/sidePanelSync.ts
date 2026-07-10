/**
 * Pure coupling logic for the Actions-column tab strip (SidePanel) vs.
 * `uiStore.mapMode`. Kept out of the store/component so the coupling rule
 * itself is trivially unit-testable without touching zustand or React.
 *
 * Rule: FLY and PLAN tabs mirror the map-click mode 1:1 (selecting one
 * flips the other, and vice versa — e.g. WaypointList's internal FLY/PLAN
 * toggle drives `setMapMode` directly, and the tab strip must follow it).
 * PARAMS and SETTINGS have no map-mode equivalent, so selecting either
 * leaves `mapMode` wherever it was, and an external `mapMode` change never
 * selects PARAMS/SETTINGS.
 */

import type { MapMode } from "./uiStore.ts";

export type SidePanelTab = "fly" | "plan" | "params" | "settings";

/** Tabs with no map-mode equivalent — selecting one leaves `mapMode` untouched. */
const MAP_MODE_NEUTRAL_TABS: ReadonlySet<SidePanelTab> = new Set(["params", "settings"]);

/**
 * The map mode that should result from selecting `tab`, given the map mode
 * currently in effect. Returns `currentMode` unchanged for the PARAMS/SETTINGS tabs.
 */
export function mapModeForTab(tab: SidePanelTab, currentMode: MapMode): MapMode {
  return MAP_MODE_NEUTRAL_TABS.has(tab) ? currentMode : (tab as MapMode);
}

/**
 * The side-panel tab that should result from `mapMode` changing, e.g. from
 * WaypointList's internal toggle. MapMode has no "params" value, so this is
 * always one of "fly"/"plan" — never leaves the previous tab at "params".
 */
export function tabForMapMode(mode: MapMode): SidePanelTab {
  return mode;
}
