/**
 * Actions-column tab strip — replaces the old always-stacked
 * ActionsPanel/WaypointList/ParamTable scroll column with a compact
 * FLY | PLAN | PARAMS strip. Only the selected panel is mounted, filling
 * the remaining height with its own internal scroll (each panel already
 * manages its own `height: 100%; overflow: auto`, see actions.css /
 * WaypointList.css / params.css).
 *
 * Tab selection is coupled to `uiStore.mapMode` (FLY/PLAN drive the map
 * click behavior; PARAMS leaves it alone) — see `sidePanelSync.ts` for the
 * pure rule and `uiStore.ts` for how both directions of the coupling wire
 * through `setSidePanelTab`/`setMapMode`.
 */

import type { ReactNode } from "react";

import { useSidePanelTab, useUiStore, type SidePanelTab } from "../store/index.ts";
import "./side-panel.css";

export interface SidePanelProps {
  flyContent: ReactNode;
  planContent: ReactNode;
  paramsContent: ReactNode;
}

const TABS: Array<{ id: SidePanelTab; label: string }> = [
  { id: "fly", label: "FLY" },
  { id: "plan", label: "PLAN" },
  { id: "params", label: "PARAMS" },
];

export function SidePanel({ flyContent, planContent, paramsContent }: SidePanelProps) {
  const tab = useSidePanelTab();
  const setSidePanelTab = useUiStore((state) => state.setSidePanelTab);

  const content = tab === "fly" ? flyContent : tab === "plan" ? planContent : paramsContent;

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
