/**
 * PARAMS/SETTINGS overlay drawer, opened from the toolbar's gear menu — QGC
 * treats Params/Settings as separate pages; a right-side drawer over the map
 * is the adapted equivalent per the layout brief. Presentation-only wrapper
 * around the existing `ParamTable`/`SettingsPanel` (their request logic is
 * untouched); visibility and which sub-tab is shown both key off the same
 * `uiStore.sidePanelTab` the toolbar's FLY/PLAN switcher uses, so there is
 * still only ever one selected tab overall.
 *
 * Closing (via the backdrop, the ✕, or picking FLY/PLAN from the toolbar)
 * always lands back on `mapMode`'s current tab ("fly" or "plan") — mapMode
 * itself is untouched by selecting params/settings (`sidePanelSync.ts`'s
 * MAP_MODE_NEUTRAL_TABS rule), so this just re-selects whichever of
 * fly/plan was already in effect underneath the overlay.
 */

import type { BridgeClient } from "../../bridge/BridgeClient.ts";
import { useSidePanelTab, useUiStore } from "../../store/index.ts";
import { ParamTable } from "../params/ParamTable.tsx";
import { SettingsPanel } from "../settings/SettingsPanel.tsx";
import "./overlay.css";

export interface OverlayDrawerProps {
  client: BridgeClient;
  vehicleId: number;
}

export function OverlayDrawer({ client, vehicleId }: OverlayDrawerProps) {
  const tab = useSidePanelTab();
  const mapMode = useUiStore((state) => state.mapMode);
  const setSidePanelTab = useUiStore((state) => state.setSidePanelTab);

  if (tab !== "params" && tab !== "settings") {
    return null;
  }

  const close = () => setSidePanelTab(mapMode);

  return (
    <div className="overlay-backdrop" onClick={close}>
      <div
        className="overlay-drawer"
        role="dialog"
        aria-label="Params and settings"
        onClick={(event) => event.stopPropagation()}
      >
        <div className="overlay-drawer-header">
          <div className="overlay-drawer-tabs" role="tablist">
            <button
              type="button"
              role="tab"
              aria-selected={tab === "params"}
              className={`overlay-drawer-tab${tab === "params" ? " overlay-drawer-tab--active" : ""}`}
              onClick={() => setSidePanelTab("params")}
            >
              Params
            </button>
            <button
              type="button"
              role="tab"
              aria-selected={tab === "settings"}
              className={`overlay-drawer-tab${tab === "settings" ? " overlay-drawer-tab--active" : ""}`}
              onClick={() => setSidePanelTab("settings")}
            >
              Settings
            </button>
          </div>
          <button type="button" className="overlay-drawer-close" onClick={close} aria-label="Close">
            {"✕"}
          </button>
        </div>
        <div className="overlay-drawer-body" role="tabpanel">
          {tab === "params" ? <ParamTable vehicleId={vehicleId} /> : <SettingsPanel client={client} />}
        </div>
      </div>
    </div>
  );
}
