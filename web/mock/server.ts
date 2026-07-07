/**
 * WebBridge mock server — implements the PROTOCOL.md §12 M1 conformance set
 * (hello auth, 1 Hz tick, telemetry subscribe/stream/unsubscribe, error
 * envelope) plus a stateful `command` lifecycle (§5) and binary H.264 video
 * streaming (§9), so the web UI can be developed against a fake ghost.
 *
 * Telemetry is played back from fixtures/telemetry.json (a scripted flight,
 * one sample per second) on a global loop clock shared by all clients; the
 * server interpolates between fixture samples to produce a smooth 10 Hz
 * stream. Video is played back from fixtures/video.h264 (looping forever,
 * per-connection playback position — see §"video fixture" below). Commands
 * are acknowledged and, for the flight-affecting actions, drive progress
 * messages and a small vehicle-state override layered on telemetry playback.
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

/**
 * Vehicle-state override layered on top of fixture playback. This is GLOBAL
 * (not per-connection) — there's exactly one simulated vehicle shared by
 * every connected client, mirroring the single shared telemetry fixture
 * clock above. `armed: null` means "use the fixture's own armed value for
 * the current playback time"; the `arm`/`disarm` commands (§5) pin it to
 * true/false, overriding the fixture until the next arm/disarm command (or a
 * server restart).
 */
const vehicleOverride: { armed: boolean | null } = { armed: null };

/** Current telemetry: fixture playback with `vehicleOverride` layered on top. */
function currentTelemetry(): TelemetryPayload {
  const base = telemetryAt(playbackTimeS());
  return vehicleOverride.armed === null ? base : { ...base, armed: vehicleOverride.armed };
}

// --- video fixture ----------------------------------------------------------
//
// fixtures/video.h264 is 8 s of H.264 baseline @ 640x360x15fps with an Access
// Unit Delimiter (NAL type 9) inserted before every access unit, a keyframe
// (IDR, NAL type 5) every 15 frames, and SPS/PPS (types 7/8) repeated inline
// before each IDR. We split it ONCE at startup into an array of access units
// (Annex-B bytes, start codes included, from one AUD up to the next) plus a
// per-AU keyframe flag, then loop that array forever at 15 fps.

interface VideoAU {
  bytes: Uint8Array;
  keyframe: boolean;
}

interface RawNal {
  /** Byte offset of the start code (00 00 01 or 00 00 00 01). */
  pos: number;
  /** Byte offset of the NAL header (first byte after the start code). */
  dataStart: number;
  nalType: number;
}

/** Scan Annex-B `buf` for every start code, returning NAL header offset + type. */
function findNals(buf: Uint8Array): RawNal[] {
  const nals: RawNal[] = [];
  const n = buf.length;
  let i = 0;
  while (i < n) {
    if (i + 4 <= n && buf[i] === 0 && buf[i + 1] === 0 && buf[i + 2] === 0 && buf[i + 3] === 1) {
      nals.push({ pos: i, dataStart: i + 4, nalType: buf[i + 4]! & 0x1f });
      i += 4;
      continue;
    }
    if (i + 3 <= n && buf[i] === 0 && buf[i + 1] === 0 && buf[i + 2] === 1) {
      nals.push({ pos: i, dataStart: i + 3, nalType: buf[i + 3]! & 0x1f });
      i += 3;
      continue;
    }
    i += 1;
  }
  return nals;
}

/** Split an Annex-B byte stream into access units at AUD (NAL type 9) boundaries. */
function splitAccessUnits(buf: Uint8Array): VideoAU[] {
  const nals = findNals(buf);
  const audPositions = nals.filter((nal) => nal.nalType === 9).map((nal) => nal.pos);
  const aus: VideoAU[] = [];
  for (let k = 0; k < audPositions.length; k++) {
    const start = audPositions[k]!;
    const end = k + 1 < audPositions.length ? audPositions[k + 1]! : buf.length;
    const keyframe = nals.some((nal) => nal.pos >= start && nal.pos < end && nal.nalType === 5);
    aus.push({ bytes: buf.subarray(start, end), keyframe });
  }
  return aus;
}

/** Raw NAL payload (header included, start code excluded) of the first NAL of `nalType` in `bytes`. */
function extractNal(bytes: Uint8Array, nals: RawNal[], nalType: number): Uint8Array {
  const idx = nals.findIndex((nal) => nal.nalType === nalType);
  if (idx < 0) {
    throw new Error(`video fixture: expected NAL type ${nalType} in first access unit`);
  }
  const start = nals[idx]!.dataStart;
  const end = idx + 1 < nals.length ? nals[idx + 1]!.pos : bytes.length;
  return bytes.subarray(start, end);
}

const hex2 = (n: number) => n.toString(16).padStart(2, "0").toUpperCase();

const videoFileBytes = new Uint8Array(
  await Bun.file(new URL("./fixtures/video.h264", import.meta.url)).arrayBuffer(),
);
const VIDEO_AUS = splitAccessUnits(videoFileBytes);
const VIDEO_FIRST_KEYFRAME_INDEX = VIDEO_AUS.findIndex((au) => au.keyframe);
if (VIDEO_FIRST_KEYFRAME_INDEX < 0) {
  throw new Error("video fixture contains no keyframe (IDR) access unit");
}
const VIDEO_FPS = 15;
const VIDEO_FRAME_INTERVAL_MS = 1000 / VIDEO_FPS;
const VIDEO_WIDTH = 640;
const VIDEO_HEIGHT = 360;

// SPS/PPS for videoConfig are lifted from the fixture's own first access unit
// (an IDR carrying in-band parameter sets), so they always match the bytes
// actually streamed.
const firstAuBytes = VIDEO_AUS[VIDEO_FIRST_KEYFRAME_INDEX]!.bytes;
const firstAuNals = findNals(firstAuBytes);
const VIDEO_SPS = extractNal(firstAuBytes, firstAuNals, 7);
const VIDEO_PPS = extractNal(firstAuBytes, firstAuNals, 8);
const VIDEO_SPS_B64 = Buffer.from(VIDEO_SPS).toString("base64");
const VIDEO_PPS_B64 = Buffer.from(VIDEO_PPS).toString("base64");
// avc1.PPCCLL per the SPS's profile_idc/constraint_flags/level_idc (bytes 1-3
// of the NAL, right after the 1-byte NAL header) — not part of PROTOCOL.md
// §9.1's documented schema (which shows plain "codec": "h264"), so this rides
// along as an additive `codecString` field; see selftest/report for details.
const VIDEO_CODEC_STRING = `avc1.${hex2(VIDEO_SPS[1]!)}${hex2(VIDEO_SPS[2]!)}${hex2(VIDEO_SPS[3]!)}`;

/** Available numeric streamIds (u8, same id in subscribe and frame header, §9.1/§9.2). */
const VIDEO_STREAMS = new Set<number>([1]);

const MAGIC_VF = 0x4656;

function buildVideoFrame(streamId: number, keyframe: boolean, timestampUs: number, au: Uint8Array): Uint8Array {
  const frame = new Uint8Array(16 + au.length);
  const dv = new DataView(frame.buffer);
  dv.setUint16(0, MAGIC_VF, true);
  frame[2] = 1; // version
  frame[3] = streamId;
  frame[4] = keyframe ? 0b0000_0001 : 0; // bit 0: keyframe; bits 1-7 reserved
  // bytes 5-7 reserved, left zero-filled
  dv.setBigUint64(8, BigInt(timestampUs), true);
  frame.set(au, 16);
  return frame;
}

// --- per-connection session ------------------------------------------------

interface Stream {
  seq: number;
  timer: ReturnType<typeof setInterval>;
}

interface VideoStream {
  cursor: number;
  timer: ReturnType<typeof setInterval>;
}

interface Session {
  authed: boolean;
  closed: boolean;
  tickTimer: ReturnType<typeof setInterval> | null;
  /** Active subscriptions keyed by "<channel>/<vehicleId>". */
  streams: Map<string, Stream>;
  /** Active video subscriptions keyed by streamId. */
  videoStreams: Map<number, VideoStream>;
  /** Pending commandProgress timers, cleared on disconnect. */
  commandTimers: Set<ReturnType<typeof setTimeout>>;
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

function stopVideoStreams(session: Session): void {
  for (const stream of session.videoStreams.values()) {
    clearInterval(stream.timer);
  }
  session.videoStreams.clear();
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

/** Validate the `id`/`channel` fields shared by every subscribe/unsubscribe message. */
function checkChannelId(ws: Socket, msg: Record<string, unknown>): { id: string; channel: string } | null {
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
  return { id, channel: msg.channel };
}

/** Validate the telemetry-specific `vehicleId` field; reply with an error and return null if bad. */
function checkTelemetryEnvelope(
  ws: Socket,
  msg: Record<string, unknown>,
  id: string,
): { id: string; vehicleId: number } | null {
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
  return { id, vehicleId: msg.vehicleId as number };
}

/** Validate the video-specific `streamId` field; reply with an error and return null if bad. */
function checkVideoEnvelope(
  ws: Socket,
  msg: Record<string, unknown>,
  id: string,
): { id: string; streamId: number } | null {
  if (typeof msg.streamId !== "number" || !Number.isInteger(msg.streamId)) {
    sendError(ws, "BAD_MESSAGE", `${msg.type} requires an integer streamId`, { id });
    return null;
  }
  if (!VIDEO_STREAMS.has(msg.streamId)) {
    sendError(ws, "UNKNOWN_STREAM", `video stream not available: ${msg.streamId}`, { id });
    return null;
  }
  return { id, streamId: msg.streamId };
}

function sendTelemetry(ws: Socket, stream: Stream, snapshot: boolean): void {
  send(ws, {
    type: "telemetry",
    channel: "telemetry",
    vehicleId: VEHICLE_ID,
    seq: stream.seq,
    snapshot,
    timeUs: nowUs(),
    ...currentTelemetry(),
  });
  stream.seq += 1;
}

function handleTelemetrySubscribe(ws: Socket, msg: Record<string, unknown>, base: { id: string }): void {
  const req = checkTelemetryEnvelope(ws, msg, base.id);
  if (!req) {
    return;
  }
  const key = `telemetry/${req.vehicleId}`;

  // Re-subscribing restarts the stream: seq back to 1 with a fresh snapshot.
  const existing = ws.data.streams.get(key);
  if (existing) {
    clearInterval(existing.timer);
  }

  send(ws, {
    type: "subscribeAck",
    id: req.id,
    channel: "telemetry",
    vehicleId: req.vehicleId,
  });

  const stream: Stream = { seq: 1, timer: 0 as unknown as ReturnType<typeof setInterval> };
  sendTelemetry(ws, stream, true);
  stream.timer = setInterval(() => sendTelemetry(ws, stream, false), TELEMETRY_INTERVAL_MS);
  ws.data.streams.set(key, stream);
}

function handleTelemetryUnsubscribe(ws: Socket, msg: Record<string, unknown>, base: { id: string }): void {
  const req = checkTelemetryEnvelope(ws, msg, base.id);
  if (!req) {
    return;
  }
  const key = `telemetry/${req.vehicleId}`;
  const stream = ws.data.streams.get(key);
  if (stream) {
    clearInterval(stream.timer);
    ws.data.streams.delete(key);
  }
  send(ws, {
    type: "unsubscribeAck",
    id: req.id,
    channel: "telemetry",
    vehicleId: req.vehicleId,
  });
}

/** §9.1: the JSON codec-config message sent on subscribe (and on SPS/PPS change — never, for this fixture). */
function sendVideoConfig(ws: Socket, streamId: number): void {
  send(ws, {
    type: "videoConfig",
    channel: "video",
    streamId,
    seq: 1,
    snapshot: true,
    timeUs: nowUs(),
    codec: "h264",
    width: VIDEO_WIDTH,
    height: VIDEO_HEIGHT,
    sps: VIDEO_SPS_B64,
    pps: VIDEO_PPS_B64,
    // Additive fields beyond §9.1's documented schema (clients MUST ignore
    // unknown fields per §13) — MSE/WebCodecs-style codec string and fps,
    // handy for a real client's <video> source buffer / pacing.
    fps: VIDEO_FPS,
    codecString: VIDEO_CODEC_STRING,
  });
}

function sendVideoFrame(ws: Socket, streamId: number, stream: VideoStream): void {
  const au = VIDEO_AUS[stream.cursor]!;
  const frame = buildVideoFrame(streamId, au.keyframe, nowUs(), au.bytes);
  ws.send(frame);
  stream.cursor = (stream.cursor + 1) % VIDEO_AUS.length;
}

function handleVideoSubscribe(ws: Socket, msg: Record<string, unknown>, base: { id: string }): void {
  const req = checkVideoEnvelope(ws, msg, base.id);
  if (!req) {
    return;
  }

  // Re-subscribing restarts the stream at the next (i.e. first) keyframe.
  const existing = ws.data.videoStreams.get(req.streamId);
  if (existing) {
    clearInterval(existing.timer);
  }

  send(ws, { type: "subscribeAck", id: req.id, channel: "video", streamId: req.streamId });
  sendVideoConfig(ws, req.streamId);

  const stream: VideoStream = {
    cursor: VIDEO_FIRST_KEYFRAME_INDEX,
    timer: 0 as unknown as ReturnType<typeof setInterval>,
  };
  stream.timer = setInterval(() => sendVideoFrame(ws, req.streamId, stream), VIDEO_FRAME_INTERVAL_MS);
  ws.data.videoStreams.set(req.streamId, stream);
}

function handleVideoUnsubscribe(ws: Socket, msg: Record<string, unknown>, base: { id: string }): void {
  const req = checkVideoEnvelope(ws, msg, base.id);
  if (!req) {
    return;
  }
  const stream = ws.data.videoStreams.get(req.streamId);
  if (stream) {
    clearInterval(stream.timer);
    ws.data.videoStreams.delete(req.streamId);
  }
  send(ws, { type: "unsubscribeAck", id: req.id, channel: "video", streamId: req.streamId });
}

function handleSubscribe(ws: Socket, msg: Record<string, unknown>): void {
  const base = checkChannelId(ws, msg);
  if (!base) {
    return;
  }
  if (base.channel === "telemetry") {
    handleTelemetrySubscribe(ws, msg, base);
    return;
  }
  if (base.channel === "video") {
    handleVideoSubscribe(ws, msg, base);
    return;
  }
  // Valid protocol channels (mission, adsb), but out of scope for this mock (§12).
  sendError(ws, "UNKNOWN_CHANNEL", `channel ${base.channel} is not implemented by the M1 mock`, {
    id: base.id,
    retryable: true,
  });
}

function handleUnsubscribe(ws: Socket, msg: Record<string, unknown>): void {
  const base = checkChannelId(ws, msg);
  if (!base) {
    return;
  }
  if (base.channel === "telemetry") {
    handleTelemetryUnsubscribe(ws, msg, base);
    return;
  }
  if (base.channel === "video") {
    handleVideoUnsubscribe(ws, msg, base);
    return;
  }
  sendError(ws, "UNKNOWN_CHANNEL", `channel ${base.channel} is not implemented by the M1 mock`, {
    id: base.id,
    retryable: true,
  });
}

/** §5.3 progress step: fired `delayMs` after the accepted commandAck. */
interface ProgressStep {
  delayMs: number;
  progress: number;
  message: string;
}

/** ~3 s of progress in 5 steps, 0.2 -> 1.0. */
function takeoffSteps(alt: number): ProgressStep[] {
  const message = `Climbing to ${alt} m`;
  return [0.2, 0.4, 0.6, 0.8, 1.0].map((progress, i) => ({
    delayMs: (i + 1) * 600,
    progress,
    message,
  }));
}

/** ~1.5 s of progress in 3 steps, 0.33 -> 1.0 (shorter than takeoff, per spec). */
function shortActionSteps(message: string): ProgressStep[] {
  return [0.33, 0.67, 1.0].map((progress, i) => ({
    delayMs: (i + 1) * 500,
    progress,
    message,
  }));
}

/** Schedule unsolicited commandProgress messages (§5.3), cancelable via `session.commandTimers`. */
function scheduleProgress(ws: Socket, id: string, vehicleId: number, steps: ProgressStep[]): void {
  for (const step of steps) {
    const timer = setTimeout(() => {
      ws.data.commandTimers.delete(timer);
      if (ws.data.closed) {
        return;
      }
      send(ws, {
        type: "commandProgress",
        id,
        vehicleId,
        progress: step.progress,
        message: step.message,
      });
    }, step.delayMs);
    ws.data.commandTimers.add(timer);
  }
}

/**
 * Command lifecycle (§5): validate envelope + action, then per-action:
 *  - `arm`/`disarm` flip `vehicleOverride.armed` (reflected in served
 *    telemetry immediately) and ack with no progress.
 *  - `takeoff`/`land`/`rtl` ack accepted then emit commandProgress messages
 *    over a few seconds (playback itself is not steered — this only affects
 *    what the client is told, plus the arm/disarm override above).
 *  - `gotoLocation`/`setFlightMode`/`pause` ack accepted with no progress.
 *  - Any action with missing/invalid params -> ack rejected with a reason.
 */
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

  const params = (typeof msg.params === "object" && msg.params !== null ? msg.params : {}) as Record<
    string,
    unknown
  >;
  const isFiniteNumber = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);

  const accept = (): void => {
    send(ws, { type: "commandAck", id, vehicleId: VEHICLE_ID, status: "accepted", mavResult: 0 });
  };
  const reject = (reason: string): void => {
    send(ws, {
      type: "commandAck",
      id,
      vehicleId: VEHICLE_ID,
      status: "rejected",
      reason,
      mavResult: 4, // MAV_RESULT_DENIED
    });
  };

  switch (msg.action) {
    case "arm":
      vehicleOverride.armed = true;
      accept();
      return;
    case "disarm":
      vehicleOverride.armed = false;
      accept();
      return;
    case "takeoff": {
      const alt = params.alt;
      if (!isFiniteNumber(alt) || alt <= 0) {
        reject("takeoff requires numeric params.alt > 0");
        return;
      }
      accept();
      scheduleProgress(ws, id, VEHICLE_ID, takeoffSteps(alt));
      return;
    }
    case "land":
      accept();
      scheduleProgress(ws, id, VEHICLE_ID, shortActionSteps("Descending to land"));
      return;
    case "rtl":
      accept();
      scheduleProgress(ws, id, VEHICLE_ID, shortActionSteps("Returning to launch"));
      return;
    case "gotoLocation":
      if (!isFiniteNumber(params.lat) || !isFiniteNumber(params.lon) || !isFiniteNumber(params.alt)) {
        reject("gotoLocation requires numeric params.lat/lon/alt");
        return;
      }
      accept();
      return;
    case "setFlightMode":
      if (typeof params.mode !== "string" || params.mode.length === 0) {
        reject("setFlightMode requires a non-empty params.mode string");
        return;
      }
      accept();
      return;
    case "pause":
      accept();
      return;
  }
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
    const session: Session = {
      authed: false,
      closed: false,
      tickTimer: null,
      streams: new Map(),
      videoStreams: new Map(),
      commandTimers: new Set(),
    };
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
      ws.data.closed = true;
      if (ws.data.tickTimer !== null) {
        clearInterval(ws.data.tickTimer);
        ws.data.tickTimer = null;
      }
      stopStreams(ws.data);
      stopVideoStreams(ws.data);
      for (const timer of ws.data.commandTimers) {
        clearTimeout(timer);
      }
      ws.data.commandTimers.clear();
    },
  },
});

console.log(
  `WebBridge mock listening on ws://${server.hostname}:${server.port}/ ` +
    `(fixture: ${FLIGHT.samples.length} samples, ${FLIGHT.loopS} s loop; ` +
    `video: ${VIDEO_AUS.length} AUs, ${VIDEO_AUS.length / VIDEO_FPS} s loop @ ${VIDEO_FPS} fps)`,
);
