/**
 * FlyView instrument strip — a compact single-row readout of the primary
 * flight instruments for one vehicle: altitude (MSL/rel), ground/air speed,
 * climb rate with tendency, heading with a mini compass rose, GPS fix/sat
 * count, and battery percent/voltage.
 *
 * Field names, units and null-handling follow PROTOCOL.md §4 (telemetry
 * channel): every leaf is `number | null`, unknown values render "—" and
 * never throw. Pure SVG/CSS — no canvas, no charting libs, no new deps.
 */

import { useVehicle } from "../../store/index.ts";
import {
  batteryLevel,
  climbGlyph,
  fmtHeading,
  fmtNum,
  fmtSigned,
  gpsFixShortLabel,
  headingCardinal,
  normalizeHeading,
} from "./instruments.ts";
import "./instruments.css";

export interface InstrumentsProps {
  vehicleId: number;
}

const COMPASS_SIZE = 34;
const COMPASS_HALF = COMPASS_SIZE / 2;
const COMPASS_NEEDLE_LEN = 12;

/** Small rotating needle + fixed N mark; unknown heading shows a dim, unrotated needle. */
function CompassRose({ headingDeg }: { headingDeg: number | null }) {
  const known = headingDeg !== null;
  const rotation = known ? normalizeHeading(headingDeg) : 0;

  return (
    <svg
      className="instrument-compass"
      viewBox={`0 0 ${COMPASS_SIZE} ${COMPASS_SIZE}`}
      role="img"
      aria-label={known ? `Heading ${fmtHeading(headingDeg)} degrees` : "Heading unknown"}
    >
      <circle
        className="instrument-compass-ring"
        cx={COMPASS_HALF}
        cy={COMPASS_HALF}
        r={COMPASS_HALF - 2}
      />
      <text className="instrument-compass-n" x={COMPASS_HALF} y={7}>
        N
      </text>
      <g
        className={`instrument-compass-needle${known ? "" : " instrument-compass-needle--unknown"}`}
        transform={`rotate(${rotation} ${COMPASS_HALF} ${COMPASS_HALF})`}
      >
        <line
          x1={COMPASS_HALF}
          y1={COMPASS_HALF}
          x2={COMPASS_HALF}
          y2={COMPASS_HALF - COMPASS_NEEDLE_LEN}
        />
        <polygon
          points={`${COMPASS_HALF},${COMPASS_HALF - COMPASS_NEEDLE_LEN - 3} ${COMPASS_HALF - 3},${COMPASS_HALF - COMPASS_NEEDLE_LEN + 2} ${COMPASS_HALF + 3},${COMPASS_HALF - COMPASS_NEEDLE_LEN + 2}`}
        />
      </g>
      <circle className="instrument-compass-hub" cx={COMPASS_HALF} cy={COMPASS_HALF} r={2} />
    </svg>
  );
}

export function Instruments({ vehicleId }: InstrumentsProps) {
  const vehicle = useVehicle(vehicleId);

  if (!vehicle) {
    return (
      <div className="instrument-strip instrument-strip--empty">
        <span className="instrument-placeholder">No instrument data</span>
      </div>
    );
  }

  const { position, velocity, attitude, gps, battery } = vehicle;
  const level = batteryLevel(battery.percent);
  const tendency = climbGlyph(velocity.climbRate);

  return (
    <div className="instrument-strip" aria-label="Flight instruments">
      <div className="instrument-cell">
        <span className="instrument-label">Altitude</span>
        <span className="instrument-value">
          {fmtNum(position.altMSL, 1)} <span className="instrument-unit">m MSL</span>
        </span>
        <span className="instrument-value instrument-value--dim">
          {fmtNum(position.altRel, 1)} <span className="instrument-unit">m REL</span>
        </span>
      </div>

      <div className="instrument-cell">
        <span className="instrument-label">Speed</span>
        <span className="instrument-value">
          {fmtNum(velocity.groundSpeed, 1)} <span className="instrument-unit">m/s GS</span>
        </span>
        <span className="instrument-value instrument-value--dim">
          {fmtNum(velocity.airSpeed, 1)} <span className="instrument-unit">m/s AS</span>
        </span>
      </div>

      <div className="instrument-cell">
        <span className="instrument-label">Climb</span>
        <span className="instrument-value">
          <span className="instrument-climb-glyph">{tendency}</span>{" "}
          {fmtSigned(velocity.climbRate)} <span className="instrument-unit">m/s</span>
        </span>
      </div>

      <div className="instrument-cell instrument-cell--heading">
        <span className="instrument-label">Heading</span>
        <div className="instrument-heading-row">
          <CompassRose headingDeg={attitude.yaw} />
          <div className="instrument-heading-text">
            <span className="instrument-value">{fmtHeading(attitude.yaw)}°</span>
            <span className="instrument-value instrument-value--dim">
              {headingCardinal(attitude.yaw)}
            </span>
          </div>
        </div>
      </div>

      <div className="instrument-cell">
        <span className="instrument-label">GPS</span>
        <span className="instrument-value">{gpsFixShortLabel(gps.fix)}</span>
        <span className="instrument-value instrument-value--dim">
          {gps.count ?? "—"} sats
        </span>
      </div>

      <div className="instrument-cell">
        <span className="instrument-label">Battery</span>
        <span className={`instrument-value instrument-value--${level}`}>
          {fmtNum(battery.percent, 0)}%
        </span>
        <span className="instrument-value instrument-value--dim">
          {fmtNum(battery.voltage, 1)} V
        </span>
      </div>
    </div>
  );
}
