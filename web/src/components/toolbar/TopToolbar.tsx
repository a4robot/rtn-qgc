/**
 * Top toolbar — QGC `FlyViewToolBar`/`FlyViewToolBarIndicators` equivalent.
 *
 * Left: the FLY/PLAN view switcher (replaces the old SidePanel tab strip for
 * those two tabs — reuses `uiStore.setSidePanelTab`/`sidePanelSync.ts`
 * unchanged, so the FLY/PLAN <-> `mapMode` coupling rule is untouched, only
 * its presentation moved here) plus compact vehicle-indicator chips (flight
 * mode + armed state, battery %, GPS fix/count, connection state) —
 * presentation-only reads of `vehicleStore`/`connectionStore`, same latitude
 * `VehicleSelect`/`NotificationBell` already take reading their stores
 * directly from a header-local control.
 *
 * Right: the notification bell (moved in from the old header, unstyled
 * otherwise), the vehicle selector, and a gear menu that opens the
 * PARAMS/SETTINGS overlay drawer by setting `sidePanelTab` to "params" or
 * "settings" — same store field the FLY/PLAN buttons use, so there is still
 * only ever one selected tab.
 */

import { useState } from "react";

import { batteryLevel, fmtNum, gpsFixShortLabel } from "../flyview/instruments.ts";
import { NotificationBell } from "../notifications/NotificationBell.tsx";
import { VehicleSelect } from "../VehicleSelect.tsx";
import { useConnection, useSidePanelTab, useUiStore, useVehicle } from "../../store/index.ts";
import "./toolbar.css";

export interface TopToolbarProps {
  activeVehicleId: number;
  onSelectVehicle: (id: number) => void;
}

export function TopToolbar({ activeVehicleId, onSelectVehicle }: TopToolbarProps) {
  const tab = useSidePanelTab();
  const setSidePanelTab = useUiStore((state) => state.setSidePanelTab);
  const vehicle = useVehicle(activeVehicleId);
  const connectionState = useConnection((state) => state.state);
  const [menuOpen, setMenuOpen] = useState(false);

  const level = batteryLevel(vehicle?.battery.percent ?? null);

  return (
    <header className="gcs-toolbar">
      <div className="gcs-toolbar-section">
        <span className="gcs-toolbar-brand">RTN Ghost GCS</span>

        <div className="toolbar-view-switch" role="group" aria-label="View">
          <button
            type="button"
            className={`toolbar-view-btn${tab === "fly" ? " toolbar-view-btn--active" : ""}`}
            aria-pressed={tab === "fly"}
            onClick={() => setSidePanelTab("fly")}
          >
            FLY
          </button>
          <button
            type="button"
            className={`toolbar-view-btn${tab === "plan" ? " toolbar-view-btn--active" : ""}`}
            aria-pressed={tab === "plan"}
            onClick={() => setSidePanelTab("plan")}
          >
            PLAN
          </button>
        </div>

        <div className="toolbar-indicators" aria-label="Vehicle indicators">
          <span className="toolbar-chip toolbar-chip--mode">{vehicle?.flightMode ?? "—"}</span>
          <span className={`toolbar-chip ${vehicle?.armed ? "toolbar-chip--armed" : "toolbar-chip--disarmed"}`}>
            {vehicle ? (vehicle.armed ? "ARMED" : "DISARMED") : "—"}
          </span>
          <span className={`toolbar-chip toolbar-chip--${level}`}>
            {fmtNum(vehicle?.battery.percent ?? null, 0)}%
          </span>
          <span className="toolbar-chip">
            {vehicle ? gpsFixShortLabel(vehicle.gps.fix) : "—"} · {vehicle?.gps.count ?? "—"}
          </span>
          <span className={`toolbar-chip toolbar-chip--conn-${connectionState}`}>{connectionState}</span>
        </div>
      </div>

      <div className="gcs-toolbar-section">
        <NotificationBell />
        <VehicleSelect activeVehicleId={activeVehicleId} onSelect={onSelectVehicle} />

        <div className="toolbar-menu">
          <button
            type="button"
            className={`toolbar-menu-button${menuOpen ? " toolbar-menu-button--active" : ""}`}
            onClick={() => setMenuOpen((open) => !open)}
            aria-expanded={menuOpen}
            aria-label="Open params/settings menu"
          >
            {"⚙"}
          </button>
          {menuOpen && (
            <div className="toolbar-menu-dropdown" role="menu">
              <button
                type="button"
                role="menuitem"
                className={`toolbar-menu-item${tab === "params" ? " toolbar-menu-item--active" : ""}`}
                onClick={() => {
                  setSidePanelTab("params");
                  setMenuOpen(false);
                }}
              >
                Params
              </button>
              <button
                type="button"
                role="menuitem"
                className={`toolbar-menu-item${tab === "settings" ? " toolbar-menu-item--active" : ""}`}
                onClick={() => {
                  setSidePanelTab("settings");
                  setMenuOpen(false);
                }}
              >
                Settings
              </button>
            </div>
          )}
        </div>
      </div>
    </header>
  );
}
