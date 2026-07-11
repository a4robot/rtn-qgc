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

export interface InstrumentColumnProps {
  vehicleId: number;
}

export function InstrumentColumn({ vehicleId }: InstrumentColumnProps) {
  return (
    <div className="instrument-column" aria-label="Instrument panel">
      <Attitude vehicleId={vehicleId} />
      <Instruments vehicleId={vehicleId} />
    </div>
  );
}
