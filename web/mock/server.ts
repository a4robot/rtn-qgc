/**
 * WebBridge mock server — implements the PROTOCOL.md §12 M1 conformance set
 * (hello auth, 1 Hz tick, telemetry subscribe/stream/unsubscribe, error
 * envelope) plus a simple accept-and-succeed `command` lifecycle, so the web
 * UI can be developed against a fake ghost.
 *
 * Telemetry is played back from fixtures/telemetry.json (a scripted flight,
 * one sample per second) on a global loop clock shared by all clients; the
 * server interpolates between fixture samples to produce a smooth 10 Hz
 * stream. Commands are acknowledged but do not steer the playback.
 *
 * Run: bun run mock   (listens on ws://127.0.0.1:8877/)
 */

import type { ServerWebSocket } from "bun";

import fixture from "./fixtures/telemetry.json";

const HOSTNAME = "127.0.0.1";
const PORT = Number(process.env.MOCK_PORT ?? 8877);
const PROTOCOL_VERSION = "0.1";
const SERVER_VERSION = "rtn-qgc-mock 0.1.0";
const TICK_INTERVAL_MS = 1000;
const TELEMETRY_INTERVAL_MS = 100; // 10 Hz

/** The mock simulates exactly one connected vehicle. */
const VEHICLE_ID = 1;
const SUBSCRIBABLE_CHANNELS = new Set(["telemetry", "mission", "video", "adsb"]);
const COMMAND_ACTIONS = new Set([
  "arm",
  "disarm",
  "takeoff",
  "land",
  "rtl",
  "gotoLocation",
  "setFlightMode",
  "pause",
]);

// --- fixture playback ------------------------------------------------------

interface TelemetryPayload {
  attitude: { roll: number; pitch: number; yaw: number };
  position: { lat: number; lon: number; altMSL: number; altRel: number };
  velocity: {
    groundSpeed: number;
    airSpeed: number | null;
    climbRate: number;
  };
  battery: { percent: number; voltage: number; current: number };
  gps: { fix: string; count: number; hdop: number };
  flightMode: string;
  armed: boolean;
}

interface FixtureSample extends TelemetryPayload {
  t: number;
}

interface Fixture {
  description: string;
  loopS: number;
  samples: FixtureSample[];
}

const FLIGHT = fixture as Fixture;
const serverStartMs = Date.now();

const nowUs = () => Date.now() * 1000;
const lerp = (a: number | null, b: number | null, f: number): number | null =>
  a === null || b === null ? a : a + (b - a) * f;
/** Shortest-arc interpolation on the 0–360 heading circle. */
const lerpYaw = (a: number, b: number, f: number): number => {
  const delta = ((b - a + 540) % 360) - 180;
  return (a + delta * f + 360) % 360;
};
const round = (x: number | null, dp: number): number | null => {
  if (x === null) return null;
  const k = 10 ** dp;
  return Math.round(x * k) / k;
};

/** Seconds into the looping fixture flight (shared by all clients). */
function playbackTimeS(): number {
  return ((Date.now() - serverStartMs) / 1000) % FLIGHT.loopS;
}

/**
 * Current telemetry state: linear interpolation between the two fixture
 * samples bracketing the playback time (discrete fields step, yaw wraps).
 */
function telemetryAt(tS: number): TelemetryPayload {
  const samples = FLIGHT.samples;
  let i = 0;
  while (i + 1 < samples.length && samples[i + 1]!.t <= tS) {
    i += 1;
  }
  const a = samples[i]!;
  const b = samples[(i + 1) % samples.length]!;
  const spanS = i + 1 < samples.length ? b.t - a.t : FLIGHT.loopS - a.t;
  const f = spanS > 0 ? Math.min((tS - a.t) / spanS, 1) : 0;

  return {
    attitude: {
      roll: round(lerp(a.attitude.roll, b.attitude.roll, f), 2) as number,
      pitch: round(lerp(a.attitude.pitch, b.attitude.pitch, f), 2) as number,
      yaw: round(lerpYaw(a.attitude.yaw, b.attitude.yaw, f), 2) as number,
    },
    position: {
      lat: round(lerp(a.position.lat, b.position.lat, f), 7) as number,
      lon: round(lerp(a.position.lon, b.position.lon, f), 7) as number,
      altMSL: round(lerp(a.position.altMSL, b.position.altMSL, f), 2) as number,
      altRel: round(lerp(a.position.altRel, b.position.altRel, f), 2) as number,
    },
    velocity: {
      groundSpeed: round(
        lerp(a.velocity.groundSpeed, b.velocity.groundSpeed, f),
        2,
      ) as number,
      airSpeed: round(lerp(a.velocity.airSpeed, b.velocity.airSpeed, f), 2),
      climbRate: round(
        lerp(a.velocity.climbRate, b.velocity.climbRate, f),
        2,
      ) as number,
    },
    battery: {
      percent: round(lerp(a.battery.percent, b.battery.percent, f), 2) as number,
      voltage: round(lerp(a.battery.voltage, b.battery.voltage, f), 2) as number,
      current: round(lerp(a.battery.current, b.battery.current, f), 2) as number,
    },
    gps: {
      fix: a.gps.fix,
      count: a.gps.count,
      hdop: round(lerp(a.gps.hdop, b.gps.hdop, f), 2) as number,
    },
    flightMode: a.flightMode,
    armed: a.armed,
  };
}

// --- per-connection session ------------------------------------------------

interface Stream {
  seq: number;
  timer: ReturnType<typeof setInterval>;
}

interface Session {
  authed: boolean;
  tickTimer: ReturnType<typeof setInterval> | null;
  /** Active subscriptions keyed by "<channel>/<vehicleId>". */
  streams: Map<string, Stream>;
}

type Socket = ServerWebSocket<Session>;

function send(ws: Socket, message: Record<string, unknown>): void {
  ws.send(JSON.stringify(message));
}

function sendError(
  ws: Socket,
  code: string,
  message: string,
  extra: { id?: string; vehicleId?: number; retryable?: boolean } = {},
): void {
  send(ws, {
    type: "error",
    ...(extra.id !== undefined ? { id: extra.id } : {}),
    code,
    message,
    ...(extra.vehicleId !== undefined ? { vehicleId: extra.vehicleId } : {}),
    retryable: extra.retryable ?? false,
  });
}

function stopStreams(session: Session): void {
  for (const stream of session.streams.values()) {
    clearInterval(stream.timer);
  }
  session.streams.clear();
}

// --- message handlers ------------------------------------------------------

function handleHello(ws: Socket, msg: Record<string, unknown>): void {
  if (msg.type !== "hello") {
    sendError(ws, "AUTH_REQUIRED", "first message must be hello");
    ws.close(1008, "hello required");
    return;
  }
  if (typeof msg.token !== "string" || msg.token.length === 0) {
    sendError(ws, "BAD_MESSAGE", "hello requires a non-empty token");
    ws.close(1008, "bad hello");
    return;
  }
  if (
    typeof msg.protocolVersion === "string" &&
    msg.protocolVersion.split(".")[0] !== PROTOCOL_VERSION.split(".")[0]
  ) {
    sendError(
      ws,
      "UNSUPPORTED_VERSION",
      `server speaks ${PROTOCOL_VERSION}, client sent ${msg.protocolVersion}`,
    );
    ws.close(1002, "unsupported protocol version");
    return;
  }

  ws.data.authed = true;
  send(ws, {
    type: "helloAck",
    protocolVersion: PROTOCOL_VERSION,
    serverVersion: SERVER_VERSION,
    serverTimeUs: nowUs(),
  });
  ws.data.tickTimer ??= setInterval(() => {
    send(ws, {
      type: "tick",
      serverTimeUs: nowUs(),
      uptimeS: Math.floor((Date.now() - serverStartMs) / 1000),
      vehicleIds: [VEHICLE_ID],
    });
  }, TICK_INTERVAL_MS);
}

/** Validate the shared subscribe/unsubscribe envelope; reply with an error and return null if bad. */
function checkSubscribeEnvelope(
  ws: Socket,
  msg: Record<string, unknown>,
): { id: string; channel: string; vehicleId: number } | null {
  const id = typeof msg.id === "string" ? msg.id : undefined;
  if (id === undefined) {
    sendError(ws, "BAD_MESSAGE", `${msg.type} requires a string id`);
    return null;
  }
  if (typeof msg.channel !== "string") {
    sendError(ws, "BAD_MESSAGE", `${msg.type} requires a channel`, { id });
    return null;
  }
  if (!SUBSCRIBABLE_CHANNELS.has(msg.channel)) {
    sendError(ws, "UNKNOWN_CHANNEL", `no such channel: ${msg.channel}`, { id });
    return null;
  }
  if (msg.channel !== "telemetry") {
    // Valid protocol channels, but out of scope for the M1 mock (§12).
    sendError(
      ws,
      "UNKNOWN_CHANNEL",
      `channel ${msg.channel} is not implemented by the M1 mock`,
      { id, retryable: true },
    );
    return null;
  }
  if (!Number.isInteger(msg.vehicleId)) {
    sendError(ws, "BAD_MESSAGE", `${msg.type} requires an integer vehicleId`, { id });
    return null;
  }
  if (msg.vehicleId !== VEHICLE_ID) {
    sendError(ws, "UNKNOWN_VEHICLE", `vehicle ${msg.vehicleId} is not connected`, {
      id,
      vehicleId: msg.vehicleId as number,
    });
    return null;
  }
  return { id, channel: msg.channel, vehicleId: msg.vehicleId as number };
}

function sendTelemetry(ws: Socket, stream: Stream, snapshot: boolean): void {
  send(ws, {
    type: "telemetry",
    channel: "telemetry",
    vehicleId: VEHICLE_ID,
    seq: stream.seq,
    snapshot,
    timeUs: nowUs(),
    ...telemetryAt(playbackTimeS()),
  });
  stream.seq += 1;
}

function handleSubscribe(ws: Socket, msg: Record<string, unknown>): void {
  const req = checkSubscribeEnvelope(ws, msg);
  if (!req) {
    return;
  }
  const key = `${req.channel}/${req.vehicleId}`;

  // Re-subscribing restarts the stream: seq back to 1 with a fresh snapshot.
  const existing = ws.data.streams.get(key);
  if (existing) {
    clearInterval(existing.timer);
  }

  send(ws, {
    type: "subscribeAck",
    id: req.id,
    channel: req.channel,
    vehicleId: req.vehicleId,
  });

  const stream: Stream = { seq: 1, timer: 0 as unknown as ReturnType<typeof setInterval> };
  sendTelemetry(ws, stream, true);
  stream.timer = setInterval(() => sendTelemetry(ws, stream, false), TELEMETRY_INTERVAL_MS);
  ws.data.streams.set(key, stream);
}

function handleUnsubscribe(ws: Socket, msg: Record<string, unknown>): void {
  const req = checkSubscribeEnvelope(ws, msg);
  if (!req) {
    return;
  }
  const key = `${req.channel}/${req.vehicleId}`;
  const stream = ws.data.streams.get(key);
  if (stream) {
    clearInterval(stream.timer);
    ws.data.streams.delete(key);
  }
  send(ws, {
    type: "unsubscribeAck",
    id: req.id,
    channel: req.channel,
    vehicleId: req.vehicleId,
  });
}

/** Command lifecycle: validate, then accept-and-succeed (playback is scripted). */
function handleCommand(ws: Socket, msg: Record<string, unknown>): void {
  const id = typeof msg.id === "string" ? msg.id : undefined;
  if (id === undefined) {
    sendError(ws, "BAD_MESSAGE", "command requires a string id");
    return;
  }
  if (!Number.isInteger(msg.vehicleId)) {
    sendError(ws, "BAD_MESSAGE", "command requires an integer vehicleId", { id });
    return;
  }
  if (msg.vehicleId !== VEHICLE_ID) {
    sendError(ws, "UNKNOWN_VEHICLE", `vehicle ${msg.vehicleId} is not connected`, {
      id,
      vehicleId: msg.vehicleId as number,
    });
    return;
  }
  if (typeof msg.action !== "string" || !COMMAND_ACTIONS.has(msg.action)) {
    sendError(ws, "BAD_MESSAGE", `unknown command action: ${String(msg.action)}`, {
      id,
      vehicleId: VEHICLE_ID,
    });
    return;
  }
  send(ws, {
    type: "commandAck",
    id,
    vehicleId: VEHICLE_ID,
    status: "accepted",
    mavResult: 0, // MAV_RESULT_ACCEPTED
  });
}

function handleMessage(ws: Socket, raw: string | Buffer): void {
  let msg: unknown;
  if (typeof raw === "string") {
    try {
      msg = JSON.parse(raw);
    } catch {
      msg = undefined;
    }
  }
  const parsed =
    typeof msg === "object" && msg !== null && typeof (msg as { type?: unknown }).type === "string"
      ? (msg as Record<string, unknown>)
      : null;

  if (!ws.data.authed) {
    // Anything that is not a well-formed hello fails authentication (§1.1).
    if (!parsed) {
      sendError(ws, "AUTH_REQUIRED", "first message must be hello");
      ws.close(1008, "hello required");
      return;
    }
    handleHello(ws, parsed);
    return;
  }

  if (!parsed) {
    sendError(
      ws,
      "BAD_MESSAGE",
      typeof raw === "string" ? "unparseable JSON or missing type" : "expected a text frame",
    );
    return;
  }

  switch (parsed.type) {
    case "hello":
      handleHello(ws, parsed); // idempotent re-hello
      break;
    case "subscribe":
      handleSubscribe(ws, parsed);
      break;
    case "unsubscribe":
      handleUnsubscribe(ws, parsed);
      break;
    case "command":
      handleCommand(ws, parsed);
      break;
    default:
      sendError(ws, "UNKNOWN_TYPE", `unrecognized type: ${parsed.type}`, {
        ...(typeof parsed.id === "string" ? { id: parsed.id } : {}),
      });
  }
}

// --- server ------------------------------------------------------------------

const server = Bun.serve<Session, never>({
  hostname: HOSTNAME,
  port: PORT,
  fetch(req, srv) {
    const session: Session = { authed: false, tickTimer: null, streams: new Map() };
    if (srv.upgrade(req, { data: session })) {
      return;
    }
    return new Response("WebBridge mock: WebSocket endpoint only", { status: 426 });
  },
  websocket: {
    message(ws, raw) {
      handleMessage(ws, raw);
    },
    close(ws) {
      if (ws.data.tickTimer !== null) {
        clearInterval(ws.data.tickTimer);
        ws.data.tickTimer = null;
      }
      stopStreams(ws.data);
    },
  },
});

console.log(
  `WebBridge mock listening on ws://${server.hostname}:${server.port}/ ` +
    `(fixture: ${FLIGHT.samples.length} samples, ${FLIGHT.loopS} s loop)`,
);
