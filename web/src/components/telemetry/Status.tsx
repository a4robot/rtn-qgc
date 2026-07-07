/**
 * Vehicle status telemetry panel — battery, GPS, flight mode/arm state,
 * speed, and link health for one vehicle. Read-only, tiled for scanability.
 */

import { useEffect, useState } from "react";

import type { GpsFix } from "../../bridge/types.ts";
import { useConnection, useVehicle } from "../../store/index.ts";
import "./status.css";

/** Telemetry older than this (ms) is considered stale even if `connected`. */
const STALE_THRESHOLD_MS = 3000;

/** Display labels for the PROTOCOL.md §4 fix strings. */
const GPS_FIX_LABELS: Record<GpsFix, string> = {
  none: "No Fix",
  "2d": "2D Fix",
  "3d": "3D Fix",
  rtkFloat: "RTK Float",
  rtkFixed: "RTK Fixed",
};

function gpsFixLabel(fix: GpsFix): string {
  return GPS_FIX_LABELS[fix] ?? fix;
}

type BatteryLevel = "ok" | "warn" | "critical";

/** >50 ok, 20-50 warn, <20 critical. */
function batteryLevel(pct: number): BatteryLevel {
  if (pct < 20) return "critical";
  if (pct < 50) return "warn";
  return "ok";
}

function formatSigned(value: number, digits = 1): string {
  const fixed = value.toFixed(digits);
  return value >= 0 ? `+${fixed}` : fixed;
}

export interface StatusProps {
  vehicleId: number;
}

export function Status({ vehicleId }: StatusProps) {
  const vehicle = useVehicle(vehicleId);
  const connectionState = useConnection((state) => state.state);

  // Re-derive staleness on a timer, since telemetry can simply stop arriving
  // without any store update to trigger a re-render.
  const [nowMs, setNowMs] = useState(() => Date.now());
  useEffect(() => {
    const id = setInterval(() => setNowMs(Date.now()), 1000);
    return () => clearInterval(id);
  }, []);

  if (!vehicle) {
    return (
      <div className="status-panel status-panel--stale">
        <span className="status-placeholder">No vehicle data</span>
      </div>
    );
  }

  const ageMs = nowMs - vehicle.lastUpdateAtMs;
  const stale = !vehicle.connected || connectionState !== "connected" || ageMs > STALE_THRESHOLD_MS;

  const level = batteryLevel(vehicle.battery.percent);
  const pctClamped = Math.max(0, Math.min(100, vehicle.battery.percent));

  return (
    <div
      className={`status-panel${stale ? " status-panel--stale" : ""}`}
      aria-label="Vehicle status"
    >
      <div className="status-tile status-tile--wide">
        <span className="status-label">Battery</span>
        <div className="status-battery-bar">
          <div
            className={`status-battery-fill status-battery-fill--${level}`}
            style={{ width: `${pctClamped}%` }}
          />
        </div>
        <span className={`status-value status-value--${level}`}>
          {vehicle.battery.percent.toFixed(0)}%
        </span>
        <span className="status-value status-value--dim">
          {vehicle.battery.voltage.toFixed(1)} V
        </span>
      </div>

      <div className="status-tile">
        <span className="status-label">GPS</span>
        <span className="status-value">{gpsFixLabel(vehicle.gps.fix)}</span>
        <span className="status-value status-value--dim">
          {vehicle.gps.count} sats · HDOP {vehicle.gps.hdop.toFixed(1)}
        </span>
      </div>

      <div className="status-tile">
        <span className="status-label">Mode</span>
        <div className="status-chip-row">
          <span className="status-chip status-chip--mode">{vehicle.flightMode}</span>
          <span
            className={`status-chip ${vehicle.armed ? "status-chip--armed" : "status-chip--disarmed"}`}
          >
            {vehicle.armed ? "ARMED" : "DISARMED"}
          </span>
        </div>
      </div>

      <div className="status-tile">
        <span className="status-label">Speed</span>
        <span className="status-value">{vehicle.velocity.groundSpeed.toFixed(1)} m/s gnd</span>
        <span className="status-value status-value--dim">
          {formatSigned(vehicle.velocity.climbRate)} m/s clb
        </span>
      </div>

      <div className="status-tile status-tile--wide">
        <span className="status-label">Link</span>
        <span
          className={`status-chip ${stale ? "status-chip--disconnected" : "status-chip--connected"}`}
        >
          {stale ? "STALE" : "LIVE"} · {connectionState}
        </span>
      </div>
    </div>
  );
}
