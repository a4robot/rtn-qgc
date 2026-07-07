/**
 * Attitude telemetry panel — artificial horizon (attitude indicator) plus
 * roll/pitch numerics and a heading readout with a small compass tape.
 *
 * Pure SVG + CSS transforms: no canvas, no charting libs. The horizon disk
 * (sky/ground + pitch ladder + bank pointer) rotates/translates inside a
 * circular clip; the aircraft symbol and bank scale stay fixed, matching a
 * conventional ADI.
 */

import { useVehicle } from "../../store/index.ts";
import "./attitude.css";

export interface AttitudeProps {
  vehicleId: number;
}

const SIZE = 200;
const HALF = SIZE / 2;
const HORIZON_RADIUS = 88;

/** Pixels of horizon travel per degree of pitch. */
const PITCH_PX_PER_DEG = 2.6;
/** Pitch ladder marks drawn above/below the horizon line (degrees). */
const PITCH_LADDER_STEPS = [-30, -20, -10, 10, 20, 30];
/** Fixed bank-angle scale marks around the top of the bezel (degrees). */
const ROLL_SCALE_DEGS = [-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60];
const ROLL_SCALE_MAJOR = new Set([-60, -30, -20, -10, 0, 10, 20, 30, 60]);

const COMPASS_WIDTH = 200;
const COMPASS_HEIGHT = 34;
const COMPASS_PX_PER_DEG = 4;
const COMPASS_SPAN_DEG = COMPASS_WIDTH / COMPASS_PX_PER_DEG / 2 + 10;

function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

function normalizeAngle(deg: number): number {
  const wrapped = deg % 360;
  return wrapped < 0 ? wrapped + 360 : wrapped;
}

/** Shortest signed difference `target - from`, wrapped into (-180, 180]. */
function angleDiff(target: number, from: number): number {
  let diff = (target - from) % 360;
  if (diff > 180) diff -= 360;
  if (diff <= -180) diff += 360;
  return diff;
}

function compassLabel(deg: number): string {
  const normalized = normalizeAngle(deg);
  switch (normalized) {
    case 0:
      return "N";
    case 90:
      return "E";
    case 180:
      return "S";
    case 270:
      return "W";
    default:
      return String(normalized).padStart(3, "0");
  }
}

/** Point on the bezel arc at `angleDeg` from top (clockwise), given radius. */
function polarPoint(angleDeg: number, radius: number): { x: number; y: number } {
  const rad = (angleDeg * Math.PI) / 180;
  return {
    x: HALF + radius * Math.sin(rad),
    y: HALF - radius * Math.cos(rad),
  };
}

function AttitudeHorizon({ rollDeg, pitchDeg }: { rollDeg: number; pitchDeg: number }) {
  const clipId = "attitude-horizon-clip";
  const pitchOffset = clamp(pitchDeg, -90, 90) * PITCH_PX_PER_DEG;

  return (
    <svg
      className="attitude-horizon"
      viewBox={`0 0 ${SIZE} ${SIZE}`}
      role="img"
      aria-label={`Artificial horizon: roll ${rollDeg.toFixed(1)} degrees, pitch ${pitchDeg.toFixed(1)} degrees`}
    >
      <defs>
        <clipPath id={clipId}>
          <circle cx={0} cy={0} r={HORIZON_RADIUS} />
        </clipPath>
      </defs>

      <g transform={`translate(${HALF} ${HALF})`}>
        <circle className="attitude-bezel" cx={0} cy={0} r={HORIZON_RADIUS + 4} />

        <g clipPath={`url(#${clipId})`}>
          <g transform={`rotate(${-rollDeg})`}>
            <g transform={`translate(0 ${pitchOffset})`}>
              <rect className="attitude-sky" x={-300} y={-600} width={600} height={600} />
              <rect className="attitude-ground" x={-300} y={0} width={600} height={600} />
              <line className="attitude-horizon-line" x1={-300} x2={300} y1={0} y2={0} />
              {PITCH_LADDER_STEPS.map((step) => {
                const y = -step * PITCH_PX_PER_DEG;
                const halfWidth = Math.abs(step) % 20 === 0 ? 30 : 16;
                return (
                  <g key={step}>
                    <line
                      className="attitude-ladder-line"
                      x1={-halfWidth}
                      x2={halfWidth}
                      y1={y}
                      y2={y}
                    />
                    <text
                      className="attitude-ladder-label"
                      x={halfWidth + 4}
                      y={y + 2.5}
                    >
                      {Math.abs(step)}
                    </text>
                  </g>
                );
              })}
            </g>

            {/* Bank pointer: rotates with roll, points to the fixed scale below. */}
            <polygon
              className="attitude-pointer"
              points={`0,${-(HORIZON_RADIUS - 6)} -5,${-(HORIZON_RADIUS - 16)} 5,${-(HORIZON_RADIUS - 16)}`}
            />
          </g>
        </g>

        {/* Fixed bank-angle scale. */}
        {ROLL_SCALE_DEGS.map((deg) => {
          const outer = polarPoint(deg, HORIZON_RADIUS + 3);
          const inner = polarPoint(deg, HORIZON_RADIUS - (ROLL_SCALE_MAJOR.has(deg) ? 12 : 7));
          return (
            <line
              key={deg}
              className={
                ROLL_SCALE_MAJOR.has(deg) ? "attitude-roll-tick-major" : "attitude-roll-tick"
              }
              x1={outer.x - HALF}
              y1={outer.y - HALF}
              x2={inner.x - HALF}
              y2={inner.y - HALF}
            />
          );
        })}
        <polygon
          className="attitude-roll-index"
          points={`0,${-(HORIZON_RADIUS + 3)} -4,${-(HORIZON_RADIUS + 11)} 4,${-(HORIZON_RADIUS + 11)}`}
        />

        {/* Fixed aircraft symbol. */}
        <g className="attitude-aircraft">
          <line x1={-28} y1={0} x2={-8} y2={0} />
          <line x1={8} y1={0} x2={28} y2={0} />
          <circle cx={0} cy={0} r={2.5} fill="var(--accent)" stroke="none" />
        </g>
      </g>
    </svg>
  );
}

function CompassTape({ headingDeg }: { headingDeg: number }) {
  const ticks: { deg: number; x: number; major: boolean }[] = [];
  for (let deg = 0; deg < 360; deg += 10) {
    const diff = angleDiff(deg, headingDeg);
    if (Math.abs(diff) <= COMPASS_SPAN_DEG) {
      ticks.push({
        deg,
        x: COMPASS_WIDTH / 2 + diff * COMPASS_PX_PER_DEG,
        major: deg % 30 === 0,
      });
    }
  }

  return (
    <svg
      className="attitude-compass"
      viewBox={`0 0 ${COMPASS_WIDTH} ${COMPASS_HEIGHT}`}
      role="img"
      aria-label={`Compass tape, heading ${headingDeg.toFixed(0)} degrees`}
    >
      {ticks.map((tick) => (
        <g key={tick.deg}>
          <line
            className={tick.major ? "attitude-compass-tick-major" : "attitude-compass-tick"}
            x1={tick.x}
            x2={tick.x}
            y1={COMPASS_HEIGHT}
            y2={tick.major ? COMPASS_HEIGHT - 12 : COMPASS_HEIGHT - 7}
          />
          {tick.major && (
            <text className="attitude-compass-label" x={tick.x} y={COMPASS_HEIGHT - 16}>
              {compassLabel(tick.deg)}
            </text>
          )}
        </g>
      ))}
      <line
        className="attitude-compass-index"
        x1={COMPASS_WIDTH / 2}
        x2={COMPASS_WIDTH / 2}
        y1={0}
        y2={COMPASS_HEIGHT}
      />
    </svg>
  );
}

export function Attitude({ vehicleId }: AttitudeProps) {
  const vehicle = useVehicle(vehicleId);

  if (!vehicle) {
    return (
      <div className="attitude attitude-empty">
        <span className="attitude-empty-label">No attitude data</span>
      </div>
    );
  }

  const { roll: rollDeg, pitch: pitchDeg, yaw: yawDeg } = vehicle.attitude;
  const heading = normalizeAngle(yawDeg);

  return (
    <div className="attitude">
      <AttitudeHorizon rollDeg={rollDeg} pitchDeg={pitchDeg} />
      <div className="attitude-numerics">
        <span>
          Roll <strong>{rollDeg.toFixed(1)}°</strong>
        </span>
        <span>
          Pitch <strong>{pitchDeg.toFixed(1)}°</strong>
        </span>
      </div>
      <div className="attitude-heading">
        <span className="attitude-heading-value">HDG {heading.toFixed(0).padStart(3, "0")}°</span>
        <CompassTape headingDeg={heading} />
      </div>
    </div>
  );
}
