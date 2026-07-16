/**
 * Vehicle selector — compact header control for multi-vehicle switching.
 * Presentation-only: reads the known vehicle ids/armed state from the
 * connection/vehicle stores to render, but the actual selection side effect
 * (updating uiStore + resubscribing telemetry) lives entirely behind the
 * `onSelect` prop — this component never imports the session or uiStore.
 *
 * Renders a chip row for a handful of vehicles (fits inline in the header)
 * and falls back to a `<select>` dropdown once there are more than
 * CHIP_ROW_MAX, so the header doesn't overflow with a large fleet.
 */

import { useConnection, useVehicles } from "../store/index.ts";
import { useTranslation } from "react-i18next";
import "./VehicleSelect.css";

/** Above this many known vehicles, switch from a chip row to a dropdown. */
const CHIP_ROW_MAX = 3;

export interface VehicleSelectProps {
  /** Vehicle currently shown across telemetry/map/actions panels. */
  activeVehicleId: number;
  /** Called with the chosen vehicle id when the user picks a different one. */
  onSelect: (id: number) => void;
}

export function VehicleSelect({ activeVehicleId, onSelect }: VehicleSelectProps) {
  const { t } = useTranslation();
  const vehicleIds = useConnection((state) => state.vehicleIds);
  const vehicles = useVehicles();

  if (vehicleIds.length === 0) {
    return (
      <div className="vehicle-select vehicle-select--empty" aria-label="Vehicle selector">
        <span className="vehicle-select-empty">{t("no vehicles")}</span>
      </div>
    );
  }

  if (vehicleIds.length <= CHIP_ROW_MAX) {
    return (
      <div className="vehicle-select vehicle-select-chips" role="group" aria-label="Vehicle selector">
        {vehicleIds.map((id) => {
          const armed = vehicles[id]?.armed ?? false;
          const active = id === activeVehicleId;
          return (
            <button
              key={id}
              type="button"
              className={`vehicle-select-chip${active ? " vehicle-select-chip--active" : ""}${
                armed ? " vehicle-select-chip--armed" : ""
              }`}
              aria-pressed={active}
              onClick={() => onSelect(id)}
            >
              #{id}
              {armed ? <span className="vehicle-select-chip-armed-dot" aria-hidden="true" /> : null}
            </button>
          );
        })}
      </div>
    );
  }

  return (
    <div className="vehicle-select vehicle-select-dropdown" aria-label="Vehicle selector">
      <select
        className="vehicle-select-native"
        value={activeVehicleId}
        onChange={(event) => onSelect(Number(event.target.value))}
      >
        {vehicleIds.map((id) => {
          const armed = vehicles[id]?.armed ?? false;
          return (
            <option key={id} value={id}>
              #{id}
              {armed ? " (ARMED)" : ""}
            </option>
          );
        })}
      </select>
    </div>
  );
}
