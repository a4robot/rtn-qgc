/**
 * Generates fixtures/telemetry-survey.json + fixtures/mission-survey.json —
 * a "recorded-style" small fixed-wing/VTOL survey flight near Zurich:
 *
 *   disarmed on ground -> arm -> vertical climb to 50 m -> 4-leg rectangular
 *   survey pattern (boustrophedon-ish box) -> RTL descent -> land -> disarm.
 *
 * Unlike fixtures/telemetry.json (the default orbit fixture, `t`-tagged
 * samples + `loopS`), this fixture uses the plain §4-leaf-set shape the mock
 * server's fixture mode (`MOCK_FIXTURE=1`) expects: one sample per second,
 * `rateHz: 1`, no envelope fields. Jitter is a deterministic hash of `t`, so
 * regenerating produces an identical file.
 *
 * Run: bun mock/fixtures/generate-survey.ts
 */

const LOOP_S = 120;
const HOME = { lat: 47.3977, lon: 8.5456, groundMSL: 430.0 };
const CRUISE_ALT_M = 50;
const METERS_PER_DEG_LAT = 111_320;

/** Phase boundaries, seconds into the flight. */
const T = {
  arm: 8,
  climbStart: 8,
  cruiseAlt: 24,
  leg1End: 43,
  leg2End: 62,
  leg3End: 81,
  leg4End: 100,
  touchdown: 116,
  disarm: 118,
};

/** Rectangular 4-leg survey box, meters north/east of HOME. Waypoints match fixtures/mission-survey.json. */
const WPTS: { t: number; north: number; east: number }[] = [
  { t: T.cruiseAlt, north: 0, east: 0 }, // survey starts back over home after the climb
  { t: T.leg1End, north: 350, east: 0 },
  { t: T.leg2End, north: 350, east: 250 },
  { t: T.leg3End, north: 80, east: 250 },
  { t: T.leg4End, north: 80, east: 30 },
  { t: T.touchdown, north: 0, east: 0 }, // RTL + descent back to home
];

const frac = (x: number) => x - Math.floor(x);
/** Deterministic noise in [-amp, +amp], stable across runs. */
const jitter = (t: number, salt: number, amp: number) =>
  (frac(Math.sin(t * 12.9898 + salt * 78.233) * 43758.5453) - 0.5) * 2 * amp;
const smoothstep = (p: number) => (p <= 0 ? 0 : p >= 1 ? 1 : p * p * (3 - 2 * p));
const round = (x: number, dp: number) => {
  const k = 10 ** dp;
  return Math.round(x * k) / k;
};
const clamp = (x: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, x));

function altRelAt(t: number): number {
  if (t < T.climbStart) return 0;
  if (t < T.cruiseAlt) return CRUISE_ALT_M * smoothstep((t - T.climbStart) / (T.cruiseAlt - T.climbStart));
  if (t < T.leg4End) return CRUISE_ALT_M;
  if (t < T.touchdown) return CRUISE_ALT_M * (1 - smoothstep((t - T.leg4End) / (T.touchdown - T.leg4End)));
  return 0;
}

/** Piecewise-linear position through WPTS, meters north/east of HOME. */
function northEastAt(t: number): { north: number; east: number } {
  if (t <= WPTS[0]!.t) return { north: WPTS[0]!.north, east: WPTS[0]!.east };
  const lastWp = WPTS[WPTS.length - 1]!;
  if (t >= lastWp.t) return { north: lastWp.north, east: lastWp.east };
  for (let i = 0; i < WPTS.length - 1; i++) {
    const a = WPTS[i]!;
    const b = WPTS[i + 1]!;
    if (t >= a.t && t <= b.t) {
      const f = (t - a.t) / (b.t - a.t);
      return { north: a.north + (b.north - a.north) * f, east: a.east + (b.east - a.east) * f };
    }
  }
  return { north: 0, east: 0 };
}

function flightModeAt(t: number): string {
  if (t < T.climbStart) return "Hold";
  if (t < T.cruiseAlt) return "Takeoff";
  if (t < T.leg4End) return "Mission";
  if (t < T.disarm) return "RTL";
  return "Hold";
}

/** Battery current draw, amps (drives the drain integration below). */
function currentAt(t: number): number {
  if (t < T.arm) return 0.8; // avionics only
  if (t < T.cruiseAlt) return 22.0; // vertical climb
  if (t < T.leg4End) return 14.0; // cruise survey legs
  if (t < T.touchdown) return 9.0; // RTL descent
  if (t < T.disarm) return 4.2; // armed, on ground briefly before disarm
  return 0.8;
}

function gpsAt(t: number): { fix: string; count: number; hdop: number } {
  const count = 12 + Math.floor(5.99 * frac(Math.sin(t * 7.13) * 1000));
  return {
    fix: "3d",
    count: clamp(count, 12, 17),
    hdop: round(clamp(0.7 + jitter(t, 5, 0.18), 0.5, 1.1), 2),
  };
}

/** Bank angle magnitude, larger near the turn at each leg boundary. */
function turnFactorAt(t: number): number {
  const boundaries = [T.leg1End, T.leg2End, T.leg3End];
  let best = 0;
  for (const b of boundaries) {
    const d = Math.abs(t - b);
    best = Math.max(best, d < 4 ? 1 - d / 4 : 0);
  }
  return best;
}

function yawAt(t: number): number {
  if (t < T.cruiseAlt) return (0 + jitter(t, 1, 1.5) + 360) % 360; // nose-up idle heading, north
  if (t < T.leg4End) {
    const before = northEastAt(t - 0.5);
    const after = northEastAt(t + 0.5);
    const deg = (Math.atan2(after.east - before.east, after.north - before.north) * 180) / Math.PI;
    return round((deg + 360) % 360, 2);
  }
  return (0 + jitter(t, 1, 1.5) + 360) % 360; // RTL: heading roughly back toward home/north
}

function airSpeedAt(t: number, groundSpeed: number): number | null {
  if (t < T.arm || t >= T.disarm) return null; // parked, no airflow
  // A handful of deliberate sensor blips during flight (§4 nullable discipline).
  const NULL_BLIPS = new Set([30, 55, 78, 92]);
  if (NULL_BLIPS.has(t)) return null;
  const wind = 1.2 + jitter(t, 6, 0.6); // headwind-ish offset over groundspeed
  return round(Math.max(0, groundSpeed + wind), 2);
}

function sampleAt(t: number, batteryPct: number): Record<string, unknown> {
  const { north, east } = northEastAt(t);
  const lat = HOME.lat + north / METERS_PER_DEG_LAT;
  const lon = HOME.lon + east / (METERS_PER_DEG_LAT * Math.cos((HOME.lat * Math.PI) / 180));
  const altRel = altRelAt(t);
  const before = northEastAt(t - 0.5);
  const after = northEastAt(t + 0.5);
  const groundSpeed = Math.hypot(after.north - before.north, after.east - before.east);
  const climbRate = altRelAt(t + 0.5) - altRelAt(t - 0.5);
  const current = currentAt(t) + (t >= T.arm && t < T.disarm ? jitter(t, 4, 0.8) : 0);

  const turn = turnFactorAt(t);
  const climbing = t >= T.climbStart && t < T.cruiseAlt;
  const descending = t >= T.leg4End && t < T.touchdown;
  const roll = turn * (12.0 * Math.sign(jitter(t, 2, 1)) || 12.0) + jitter(t, 2, 1.0);
  const pitch = climbing ? 8.0 + jitter(t, 3, 1.0) : descending ? -4.0 + jitter(t, 3, 1.0) : -2.0 + jitter(t, 3, 0.6);

  return {
    attitude: { roll: round(roll, 2), pitch: round(pitch, 2), yaw: round(yawAt(t), 2) },
    position: {
      lat: round(lat, 7),
      lon: round(lon, 7),
      altMSL: round(HOME.groundMSL + altRel, 2),
      altRel: round(altRel, 2),
    },
    velocity: {
      groundSpeed: round(groundSpeed, 2),
      airSpeed: airSpeedAt(t, groundSpeed),
      climbRate: round(climbRate, 2),
    },
    battery: {
      percent: round(batteryPct, 2),
      voltage: round(21.0 + 4.2 * (batteryPct / 100) - 0.02 * current, 2),
      current: round(current, 2),
    },
    gps: gpsAt(t),
    flightMode: flightModeAt(t),
    armed: t >= T.arm && t < T.disarm,
  };
}

// Battery drain: 100% -> 82% over the fixture, weighted by the current draw
// profile above (dynamically solved so the endpoints land exactly on 100/82
// regardless of tweaks to `currentAt`/phase boundaries).
let totalAmpSeconds = 0;
for (let t = 0; t < LOOP_S; t++) totalAmpSeconds += currentAt(t);
const DRAIN_PCT = 18;
const BATTERY_K = DRAIN_PCT / totalAmpSeconds;

const samples: Record<string, unknown>[] = [];
let batteryPct = 100;
for (let t = 0; t < LOOP_S; t++) {
  samples.push(sampleAt(t, batteryPct));
  batteryPct -= currentAt(t) * BATTERY_K;
}

const telemetryJson = `{
  "description": "Recorded-style 120 s survey flight near Zurich (47.3977, 8.5456): disarmed on ground -> arm -> vertical climb to 50 m -> 4-leg rectangular survey pattern -> RTL descent -> land -> disarm. One sample per second (rateHz 1); the mock server's fixture mode (MOCK_FIXTURE=1) interpolates to 10 Hz and loops. Generated by generate-survey.ts - do not hand-edit.",
  "rateHz": 1,
  "samples": [
${samples.map((s) => "    " + JSON.stringify(s)).join(",\n")}
  ]
}
`;

const telemetryPath = new URL("./telemetry-survey.json", import.meta.url).pathname;
await Bun.write(telemetryPath, telemetryJson);
console.log(`wrote ${samples.length} samples to ${telemetryPath}`);

// --- matching 6-waypoint mission (§7.1): takeoff, 4 survey waypoints, RTL.

const MAV_CMD_NAV_WAYPOINT = 16;
const MAV_CMD_NAV_RETURN_TO_LAUNCH = 20;
const MAV_CMD_NAV_TAKEOFF = 22;
const FRAME_GLOBAL_RELATIVE_ALT = 6;

function toLatLon(north: number, east: number): { lat: number; lon: number } {
  return {
    lat: round(HOME.lat + north / METERS_PER_DEG_LAT, 7),
    lon: round(HOME.lon + east / (METERS_PER_DEG_LAT * Math.cos((HOME.lat * Math.PI) / 180)), 7),
  };
}

function missionItem(
  seq: number,
  command: number,
  latlon: { lat: number; lon: number },
  alt: number,
): Record<string, unknown> {
  return {
    seq,
    frame: FRAME_GLOBAL_RELATIVE_ALT,
    command,
    current: seq === 0,
    autoContinue: true,
    param1: 0,
    param2: 0,
    param3: 0,
    param4: 0,
    lat: latlon.lat,
    lon: latlon.lon,
    alt,
  };
}

// Survey waypoints (skip the synthetic re-visit-home entry at WPTS[0]/[5]
// used only to shape the telemetry climb/descent legs above).
const surveyLegs = WPTS.slice(1, 5);
const missionItems = [
  missionItem(0, MAV_CMD_NAV_TAKEOFF, toLatLon(0, 0), CRUISE_ALT_M),
  ...surveyLegs.map((wp, i) => missionItem(i + 1, MAV_CMD_NAV_WAYPOINT, toLatLon(wp.north, wp.east), CRUISE_ALT_M)),
  // RTL's lat/lon are 0,0 (no meaningful coordinate — see fixtures/telemetry.json's
  // twin convention in server.ts's defaultMissionItems()).
  missionItem(5, MAV_CMD_NAV_RETURN_TO_LAUNCH, { lat: 0, lon: 0 }, 0),
];

const missionJson = `{
  "description": "6-item survey mission matching fixtures/telemetry-survey.json: takeoff to 50 m, 4-leg rectangular survey box, RTL. Generated by generate-survey.ts - do not hand-edit.",
  "items": ${JSON.stringify(missionItems, null, 2).split("\n").map((l, i) => (i === 0 ? l : "  " + l)).join("\n")}
}
`;

const missionPath = new URL("./mission-survey.json", import.meta.url).pathname;
await Bun.write(missionPath, missionJson);
console.log(`wrote ${missionItems.length} mission items to ${missionPath}`);
