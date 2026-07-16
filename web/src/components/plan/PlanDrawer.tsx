/**
 * Plan-mode left drawer, floating over the full-viewport map — QGC's Plan
 * view puts the mission list on the left. Thin presentation wrapper around
 * the existing `WaypointList` (its upload/clear/mission request logic is
 * untouched); visible whenever the toolbar's FLY/PLAN switcher has selected
 * "plan" (`uiStore.sidePanelTab`, unchanged coupling to `mapMode` via
 * `sidePanelSync.ts`).
 */

import type { BridgeClient } from "../../bridge/BridgeClient.ts";
import { useSidePanelTab, useUiStore } from "../../store/index.ts";
import { WaypointList } from "./WaypointList.tsx";
import { useTranslation } from "react-i18next";
import "./plan-drawer.css";

export interface PlanDrawerProps {
  client: BridgeClient;
  vehicleId: number;
}

export function PlanDrawer({ client, vehicleId }: PlanDrawerProps) {
  const { t } = useTranslation();
  const tab = useSidePanelTab();
  const setSidePanelTab = useUiStore((state) => state.setSidePanelTab);

  if (tab !== "plan") {
    return null;
  }

  return (
    <div className="plan-drawer" aria-label="Mission plan">
      <div className="plan-drawer-header">
        <span className="plan-drawer-title">{t("Mission Plan")}</span>
        <button
          type="button"
          className="plan-drawer-close"
          onClick={() => setSidePanelTab("fly")}
          aria-label="Close mission plan"
        >
          {"✕"}
        </button>
      </div>
      <div className="plan-drawer-body">
        <WaypointList client={client} vehicleId={vehicleId} />
      </div>
    </div>
  );
}
