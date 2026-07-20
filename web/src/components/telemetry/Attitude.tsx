/**
 * Attitude telemetry panel — artificial horizon (attitude indicator) plus
 * roll/pitch numerics and a heading readout with a small compass tape.
 *
 * Pure SVG + CSS transforms: no canvas, no charting libs. The horizon disk
 * (sky/ground + pitch ladder + bank pointer) rotates/translates inside a
 * circular clip; the aircraft symbol and bank scale stay fixed, matching a
 * conventional ADI.
 */

import type maplibregl from "maplibre-gl";
import { useVehicle } from "../../store/index.ts";
import { useTranslation } from "react-i18next";
import "./attitude.css";

export interface AttitudeProps {
  vehicleId: number;
  map?: maplibregl.Map | null;
}

const SIZE = 240;
const HALF = SIZE / 2;
const HORIZON_RADIUS = 75;
const RING_INNER = 75;
const RING_OUTER = 100;
const COMPASS_OUTER = 120;

/** Pixels of horizon travel per degree of pitch. */
const PITCH_PX_PER_DEG = 2.6;
const PITCH_LADDER_STEPS = [-30, -20, -10, 10, 20, 30];

function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

function normalizeAngle(deg: number): number {
  const wrapped = deg % 360;
  return wrapped < 0 ? wrapped + 360 : wrapped;
}

/** Point on the bezel arc at `angleDeg` from top (clockwise), given radius. */
function polarPoint(angleDeg: number, radius: number): { x: number; y: number } {
  const rad = (angleDeg * Math.PI) / 180;
  return {
    x: radius * Math.sin(rad),
    y: -radius * Math.cos(rad),
  };
}

function AttitudeHorizon({ rollDeg, pitchDeg, headingDeg }: { rollDeg: number; pitchDeg: number; headingDeg: number }) {
  const clipId = "attitude-horizon-clip";
  const pitchOffset = clamp(pitchDeg, -90, 90) * PITCH_PX_PER_DEG;

  // Static inner ring segments (Google Earth style buttons)
  const segments = [
    { text: "LOOK", angle: -45 },
    { text: "ZOOM", angle: 45 },
    { text: "VIEW", angle: 90 },
    { text: "VIEW", angle: 135 },
    { text: "PAN", angle: 180 },
    { text: "SCALE DOWN", angle: -135 },
    { text: "SCALE UP", angle: -90 }
  ];

  const compassTicks = [];
  for (let i = 0; i < 360; i += 10) {
    const isMajor = i % 90 === 0;
    const isMedium = i % 30 === 0;
    compassTicks.push({ deg: i, isMajor, isMedium });
  }

  const getCompassLabel = (deg: number) => {
    switch (deg) {
      case 0: return "N";
      case 90: return "E";
      case 180: return "S";
      case 270: return "W";
      default: return "";
    }
  };

  return (
    <svg
      className="attitude-horizon"
      viewBox={`0 0 ${SIZE} ${SIZE}`}
      role="img"
      aria-label={`Artificial horizon: roll ${rollDeg.toFixed(1)}, pitch ${pitchDeg.toFixed(1)}, heading ${headingDeg.toFixed(0)}`}
    >
      <defs>
        <clipPath id={clipId}>
          <circle cx={0} cy={0} r={HORIZON_RADIUS} />
        </clipPath>
        
        {/* Curved text path for segments */}
        <path id="ring-text-path-top" d={`M ${-RING_INNER - 8},0 A ${RING_INNER + 8},${RING_INNER + 8} 0 0,1 ${RING_INNER + 8},0`} />
        <path id="ring-text-path-bottom" d={`M ${RING_INNER + 8},0 A ${RING_INNER + 8},${RING_INNER + 8} 0 0,1 ${-RING_INNER - 8},0`} />
      </defs>

      <g transform={`translate(${HALF} ${HALF})`}>
        
        {/* 1. The rotating compass ring (Outermost) */}
        <g transform={`rotate(${-headingDeg})`}>
          <circle cx={0} cy={0} r={COMPASS_OUTER} fill="#3a3a3a" stroke="#222" strokeWidth="2" />
          {compassTicks.map((tick) => {
            const innerR = tick.isMajor ? COMPASS_OUTER - 15 : (tick.isMedium ? COMPASS_OUTER - 10 : COMPASS_OUTER - 5);
            const outerR = COMPASS_OUTER;
            const p1 = polarPoint(tick.deg, innerR);
            const p2 = polarPoint(tick.deg, outerR);
            const labelPos = polarPoint(tick.deg, COMPASS_OUTER - 22);
            return (
              <g key={tick.deg}>
                <line x1={p1.x} y1={p1.y} x2={p2.x} y2={p2.y} stroke="#fff" strokeWidth={tick.isMajor ? 2 : 1} />
                {tick.isMajor && (
                  <text x={labelPos.x} y={labelPos.y} fill="#fff" fontSize="16" fontWeight="bold" textAnchor="middle" dominantBaseline="central" transform={`rotate(${tick.deg} ${labelPos.x} ${labelPos.y})`}>
                    {getCompassLabel(tick.deg)}
                  </text>
                )}
              </g>
            );
          })}
        </g>

        {/* 2. Fixed Inner bezel with segments */}
        <circle cx={0} cy={0} r={RING_OUTER} fill="#444" stroke="#222" strokeWidth="2" />
        <circle cx={0} cy={0} r={RING_INNER} fill="none" stroke="#222" strokeWidth="2" />
        
        {/* Draw segment separators */}
        {segments.map((seg, i) => {
          const p1 = polarPoint(seg.angle - 22.5, RING_INNER);
          const p2 = polarPoint(seg.angle - 22.5, RING_OUTER);
          return <line key={i} x1={p1.x} y1={p1.y} x2={p2.x} y2={p2.y} stroke="#222" strokeWidth="2" />;
        })}

        {/* Draw segment texts (simplified without path for now, just rotated) */}
        {segments.map((seg, i) => {
          const p = polarPoint(seg.angle, (RING_INNER + RING_OUTER) / 2);
          let rot = seg.angle;
          if (rot > 90 || rot < -90) rot += 180;
          return (
            <text key={i} x={p.x} y={p.y} fill="#ddd" fontSize="10" fontWeight="bold" textAnchor="middle" dominantBaseline="central" transform={`rotate(${rot} ${p.x} ${p.y})`}>
              {seg.text}
            </text>
          );
        })}

        {/* Top fixed Teal Arrow indicating Current Heading */}
        <polygon points="-8,-100 8,-100 0,-115" fill="#43c6b8" stroke="#111" strokeWidth="1" />
        <rect x="-12" y="-100" width="24" height="14" fill="#444" stroke="#222" strokeWidth="1" />
        <text x="0" y="-93" fill="#43c6b8" fontSize="10" fontWeight="bold" textAnchor="middle" dominantBaseline="central">N</text>

        {/* 3. The Artificial Horizon */}
        <g clipPath={`url(#${clipId})`}>
          <g transform={`rotate(${-rollDeg})`}>
            <g transform={`translate(0 ${pitchOffset})`}>
              <rect className="attitude-sky" x={-200} y={-400} width={400} height={400} />
              <rect className="attitude-ground" x={-200} y={0} width={400} height={400} />
              
              {/* Perspective grid for the ground (Spherical illusion) */}
              <g className="attitude-ground-grid">
                {/* Radiating perspective lines */}
                {[-80, -60, -40, -20, 0, 20, 40, 60, 80].map(angle => {
                  const rad = angle * Math.PI / 180;
                  const x2 = 600 * Math.tan(rad);
                  return <line key={`v-${angle}`} x1={0} y1={0} x2={x2} y2={600} />;
                })}
                {/* Curved latitude lines */}
                {[15, 35, 65, 105, 155, 215, 285, 365, 460].map(y => (
                  <path key={`h-${y}`} d={`M -400, ${y * 0.4} Q 0, ${y * 1.3} 400, ${y * 0.4}`} fill="none" />
                ))}
              </g>

              <line className="attitude-horizon-line" x1={-200} x2={200} y1={0} y2={0} />
              {PITCH_LADDER_STEPS.map((step) => {
                const y = -step * PITCH_PX_PER_DEG;
                const halfWidth = 25;
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
                      x={halfWidth + 8}
                      y={y}
                      dominantBaseline="central"
                    >
                      {Math.abs(step)}
                    </text>
                  </g>
                );
              })}
            </g>
          </g>
        </g>

        {/* Fixed Aircraft Symbol (Teal Crosshair) */}
        <circle cx={0} cy={0} r={4} fill="#43c6b8" />
        <rect x={-40} y={-1.5} width={30} height={3} fill="#43c6b8" rx={1.5} />
        <rect x={10} y={-1.5} width={30} height={3} fill="#43c6b8" rx={1.5} />
      </g>
    </svg>
  );
}

export function Attitude({ vehicleId, map }: AttitudeProps) {
  const { t } = useTranslation();
  const vehicle = useVehicle(vehicleId);

  if (!vehicle) {
    return (
      <div className="attitude attitude-empty">
        <span className="attitude-empty-label">{t("No attitude data")}</span>
      </div>
    );
  }

  // §4: attitude fields are null until the first attitude message; render level.
  const rollDeg = vehicle.attitude.roll ?? 0;
  const pitchDeg = vehicle.attitude.pitch ?? 0;
  const yawDeg = vehicle.attitude.yaw ?? 0;
  const heading = normalizeAngle(yawDeg);

  const handleMouseDown = (e: React.MouseEvent) => {
    if (!map) return;
    // Don't intercept right clicks
    if (e.button === 2) return;
    
    e.preventDefault();
    const startX = e.clientX;
    const startY = e.clientY;
    const startPitch = map.getPitch();
    const startBearing = map.getBearing();

    const onMouseMove = (moveEvent: MouseEvent) => {
      const dx = moveEvent.clientX - startX;
      const dy = moveEvent.clientY - startY;

      // Drag up (negative dy) tilts the map up (increases pitch)
      const newPitch = Math.max(0, Math.min(85, startPitch - dy * 0.5));
      // Drag right (positive dx) rotates the map right (decreases bearing)
      const newBearing = startBearing - dx * 0.5;

      map.setPitch(newPitch);
      map.setBearing(newBearing);
    };

    const onMouseUp = () => {
      document.removeEventListener("mousemove", onMouseMove);
      document.removeEventListener("mouseup", onMouseUp);
      document.body.style.cursor = "";
    };

    document.addEventListener("mousemove", onMouseMove);
    document.addEventListener("mouseup", onMouseUp);
    document.body.style.cursor = "grabbing";
  };

  return (
    <div
      className="attitude"
      onMouseDown={handleMouseDown}
      style={{ cursor: map ? "grab" : "default" }}
      title={map ? "Drag to adjust 3D map view" : undefined}
    >
      <AttitudeHorizon rollDeg={rollDeg} pitchDeg={pitchDeg} headingDeg={heading} />
      {/* We removed the CompassTape since the ring acts as the compass */}
      <div className="attitude-numerics">
        <span>
          {t("Roll")} <strong>{rollDeg.toFixed(1)}°</strong>
        </span>
        <span>
          {t("Pitch")} <strong>{pitchDeg.toFixed(1)}°</strong>
        </span>
        <span>
          {t("Yaw")} <strong>{heading.toFixed(0).padStart(3, "0")}°</strong>
        </span>
      </div>
    </div>
  );
}
