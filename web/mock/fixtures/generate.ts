/**
 * Generates fixtures/telemetry.json — a simulated 120 s flight:
 *
 *   boot (GPS acquiring) → arm → takeoff to 30 m → one full 80 m-radius
 *   orbit → RTL descent → touchdown → disarm → idle.
 *
 * One sample per second, each matching the PROTOCOL.md §4 telemetry payload
 * schema exactly (plus a `t` seconds-into-flight key). The mock server
 * interpolates between samples at 10 Hz and loops. Jitter is a deterministic
 * hash of `t`, so regenerating produces an identical file.
 *
 * Run: bun mock/fixtures/generate.ts
 */

const LOOP_S = 120;
const HOME = { lat: 13.7367, lon: 100.5232, groundMSL: 32.0 };
const CRUISE_ALT_M = 30;
const ORBIT_RADIUS_M = 80;
const ORBIT_CENTER = { north: ORBIT_RADIUS_M, east: 0 }; // 80 m north of home

/** Phase boundaries, seconds into the loop. */
const T = { armed: 5, takeoff: 8, orbit: 20, rtl: 90, touchdown: 104, disarm: 106 };

const METERS_PER_DEG_LAT = 111_320;

const frac = (x: number) => x - Math.floor(x);
/** Deterministic noise in [-amp, +amp], stable across runs. */
const jitter = (t: number, salt: number, amp: number) =>
  (frac(Math.sin(t * 12.9898 + salt * 78.233) * 43758.5453) - 0.5) * 2 * amp;
const smoothstep = (p: number) =>
  p <= 0 ? 0 : p >= 1 ? 1 : p * p * (3 - 2 * p);
const round = (x: number, dp: number) => {
  const k = 10 ** dp;
  return Math.round(x * k) / k;
};

function altRelAt(t: number): number {
  if (t < T.takeoff) return 0;
  if (t < T.orbit) {
    return CRUISE_ALT_M * smoothstep((t - T.takeoff) / (T.orbit - T.takeoff));
  }
  if (t < T.rtl) return CRUISE_ALT_M;
  if (t < T.touchdown) {
    return CRUISE_ALT_M * (1 - smoothstep((t - T.rtl) / (T.touchdown - T.rtl)));
  }
  return 0;
}

/** Horizontal offset from home, meters. One full CCW lap during the orbit phase. */
function northEastAt(t: number): { north: number; east: number } {
  if (t < T.orbit || t >= T.rtl) {
    return { north: 0, east: 0 };
  }
  // theta = pi puts the vehicle on the circle exactly at home, so the
  // takeoff -> orbit and orbit -> RTL transitions are position-continuous.
  const theta = Math.PI + (2 * Math.PI * (t - T.orbit)) / (T.rtl - T.orbit);
  return {
    north: ORBIT_CENTER.north + ORBIT_RADIUS_M * Math.cos(theta),
    east: ORBIT_CENTER.east + ORBIT_RADIUS_M * Math.sin(theta),
  };
}

function flightModeAt(t: number): string {
  if (t < T.takeoff) return "Hold";
  if (t < T.orbit) return "Takeoff";
  if (t < T.rtl) return "Orbit";
  if (t < T.disarm) return "RTL";
  return "Hold";
}

/** Battery current draw, amps (drives the drain integration below). */
function currentAt(t: number): number {
  if (t < T.armed) return 0.8; // avionics only
  if (t < T.takeoff) return 4.2; // armed, motors idling
  if (t < T.orbit) return 24.0; // climb
  if (t < T.rtl) return 15.0; // orbit cruise
  if (t < T.touchdown) return 10.0; // descent
  if (t < T.disarm) return 4.2;
  return 0.8;
}

function gpsAt(t: number): { fix: string; count: number; hdop: number } {
  if (t < 1) return { fix: "none", count: 0, hdop: 99.9 };
  if (t < 3) return { fix: "2d", count: 6, hdop: 2.6 };
  if (t < 5) return { fix: "3d", count: 11, hdop: 1.2 };
  return {
    fix: "3d",
    count: 13 + Math.floor(2.99 * frac(Math.sin(t * 7.13) * 1000)),
    hdop: round(0.7 + jitter(t, 5, 0.12), 2),
  };
}

function yawAt(t: number): number {
  if (t < T.orbit) return (90 + jitter(t, 1, 1.5) + 360) % 360; // parked heading east
  if (t < T.rtl) {
    // Orbit noses in toward the circle center.
    const { north, east } = northEastAt(t);
    const deg =
      (Math.atan2(ORBIT_CENTER.east - east, ORBIT_CENTER.north - north) * 180) /
      Math.PI;
    return round((deg + 360) % 360, 2);
  }
  return (0 + jitter(t, 1, 1.5) + 360) % 360; // ends the lap facing the center (north)
}

function sampleAt(t: number, batteryPct: number): Record<string, unknown> {
  const flying = t >= T.takeoff && t < T.touchdown;
  const { north, east } = northEastAt(t);
  const lat = HOME.lat + north / METERS_PER_DEG_LAT;
  const lon =
    HOME.lon + east / (METERS_PER_DEG_LAT * Math.cos((HOME.lat * Math.PI) / 180));
  const altRel = altRelAt(t);
  const before = northEastAt(t - 0.5);
  const after = northEastAt(t + 0.5);
  const groundSpeed = Math.hypot(after.north - before.north, after.east - before.east);
  const climbRate = altRelAt(t + 0.5) - altRelAt(t - 0.5);
  const current = currentAt(t) + (flying ? jitter(t, 4, 0.8) : 0);

  const inOrbit = t >= T.orbit && t < T.rtl;
  const roll = inOrbit ? 6.0 + jitter(t, 2, 1.0) : jitter(t, 2, 0.5);
  const pitch =
    t >= T.takeoff && t < T.orbit
      ? -3.0 + jitter(t, 3, 0.8)
      : inOrbit
        ? -1.5 + jitter(t, 3, 0.8)
        : jitter(t, 3, 0.5);

  return {
    t,
    attitude: { roll: round(roll, 2), pitch: round(pitch, 2), yaw: round(yawAt(t), 2) },
    position: {
      lat: round(lat, 7),
      lon: round(lon, 7),
      altMSL: round(HOME.groundMSL + altRel, 2),
      altRel: round(altRel, 2),
    },
    velocity: {
      groundSpeed: round(groundSpeed, 2),
      airSpeed: null, // multirotor: no airspeed sensor
      climbRate: round(climbRate, 2),
    },
    battery: {
      percent: round(batteryPct, 2),
      voltage: round(21.0 + 4.2 * (batteryPct / 100) - 0.02 * current, 2),
      current: round(current, 2),
    },
    gps: gpsAt(t),
    flightMode: flightModeAt(t),
    armed: t >= T.armed && t < T.disarm,
  };
}

const samples: Record<string, unknown>[] = [];
let batteryPct = 100;
for (let t = 0; t < LOOP_S; t++) {
  samples.push(sampleAt(t, batteryPct));
  batteryPct -= currentAt(t) * 0.0075; // %/s per amp drawn
}

const json = `{
  "description": "Simulated 120 s multirotor flight: boot -> arm -> takeoff to 30 m -> one 80 m-radius orbit -> RTL -> touchdown -> disarm. One sample per second; the mock server interpolates to 10 Hz and loops (battery resets at the loop seam). Generated by generate.ts - do not hand-edit.",
  "loopS": ${LOOP_S},
  "samples": [
${samples.map((s) => "    " + JSON.stringify(s)).join(",\n")}
  ]
}
`;

const outPath = new URL("./telemetry.json", import.meta.url).pathname;
await Bun.write(outPath, json);
console.log(`wrote ${samples.length} samples to ${outPath}`);
