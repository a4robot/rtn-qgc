/**
 * Actions-column tab strip — replaces the old always-stacked
 * ActionsPanel/WaypointList/ParamTable scroll column with a compact
 * FLY | PLAN | PARAMS | SETTINGS strip. Only the selected panel is mounted,
 * filling the remaining height with its own internal scroll (each panel
 * already manages its own `height: 100%; overflow: auto`, see actions.css /
 * WaypointList.css / params.css / settings.css).
 *
 * Tab selection is coupled to `uiStore.mapMode` (FLY/PLAN drive the map
 * click behavior; PARAMS/SETTINGS leave it alone) — see `sidePanelSync.ts`
 * for the pure rule and `uiStore.ts` for how both directions of the
 * coupling wire through `setSidePanelTab`/`setMapMode`.
 */

import type { ReactNode } from "react";

import { useSidePanelTab, useUiStore, type SidePanelTab } from "../store/index.ts";
import "./side-panel.css";

export interface SidePanelProps {
  flyContent: ReactNode;
  planContent: ReactNode;
  paramsContent: ReactNode;
  settingsContent: ReactNode;
}

const TABS: Array<{ id: SidePanelTab; label: string }> = [
  { id: "fly", label: "FLY" },
  { id: "plan", label: "PLAN" },
  { id: "params", label: "PARAMS" },
  { id: "settings", label: "SETTINGS" },
];

export function SidePanel({ flyContent, planContent, paramsContent, settingsContent }: SidePanelProps) {
  const tab = useSidePanelTab();
  const setSidePanelTab = useUiStore((state) => state.setSidePanelTab);

  const content =
    tab === "fly" ? flyContent : tab === "plan" ? planContent : tab === "params" ? paramsContent : settingsContent;

  return (
    <div className="side-panel">
      <div className="side-panel-tabs" role="tablist" aria-label="Actions panel tabs">
        {TABS.map(({ id, label }) => (
          <button
            key={id}
            type="button"
            role="tab"
            aria-selected={tab === id}
            className={`side-panel-tab${tab === id ? " side-panel-tab--active" : ""}`}
            onClick={() => setSidePanelTab(id)}
          >
            {label}
          </button>
        ))}
      </div>
      <div className="side-panel-content" role="tabpanel">
        {content}
      </div>
    </div>
  );
}
