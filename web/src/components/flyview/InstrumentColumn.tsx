/**
 * Right-edge floating instrument column — QGC's `FlyViewInstrumentPanel`
 * placement. Pure composition: stacks the existing `Attitude` (horizon +
 * heading tape) above the existing `Instruments` strip, re-flowed vertically
 * via `instrument-column.css`'s scoped override. Neither child component is
 * modified — this is a presentation wrapper only.
 */

import { Attitude } from "../telemetry/Attitude.tsx";
import { Instruments } from "./Instruments.tsx";
import "./instrument-column.css";

import type maplibregl from "maplibre-gl";

export interface InstrumentColumnProps {
  vehicleId: number;
  map?: maplibregl.Map | null;
}

export function InstrumentColumn({ vehicleId, map }: InstrumentColumnProps) {
  return (
    <div className="instrument-column" aria-label="Instrument panel">
      <Attitude vehicleId={vehicleId} map={map} />
      <Instruments vehicleId={vehicleId} />
    </div>
  );
}
