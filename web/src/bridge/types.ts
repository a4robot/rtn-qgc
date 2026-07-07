/**
 * Bridge protocol types — state-not-events.
 *
 * The bridge publishes full state snapshots per channel; the UI renders the
 * latest state and never reconstructs it from event history. Every channel
 * message carries a monotonically increasing per-channel `seq` so the client
 * can detect gaps (a gap means "request a fresh snapshot", never "replay").
 * Every vehicle-scoped message carries a `vehicleId`.
 */

/** Channels the bridge publishes on. */
export type Channel = "telemetry" | "tick" | "command" | "video";

/** Fields common to every message on any channel. */
export interface ChannelMessage {
  /** Which channel this message belongs to. */
  channel: Channel;
  /** Monotonic per-channel sequence number for gap detection. */
  seq: number;
  /** Bridge-side timestamp, milliseconds since epoch. */
  timestampMs: number;
}

/** Fields common to every vehicle-scoped message. */
export interface VehicleMessage extends ChannelMessage {
  /** MAVLink system id of the vehicle this state belongs to. */
  vehicleId: number;
}

/** GPS fix quality, PROTOCOL.md §4. */
export type GpsFix = "none" | "2d" | "3d" | "rtkFloat" | "rtkFixed";

/**
 * Full telemetry state snapshot for one vehicle — field names and units are
 * exactly PROTOCOL.md §4 (degrees, meters, m/s; unknown values are null).
 * Link liveness is not part of the payload; it is derived from the tick's
 * vehicleIds (see vehicleStore).
 */
export interface Telemetry extends VehicleMessage {
  channel: "telemetry";
  armed: boolean;
  flightMode: string;
  position: {
    lat: number | null;
    lon: number | null;
    altMSL: number | null;
    altRel: number | null;
  };
  attitude: {
    roll: number | null;
    pitch: number | null;
    /** 0–360, heading. */
    yaw: number | null;
  };
  velocity: {
    groundSpeed: number | null;
    /** null on vehicles without an airspeed sensor. */
    airSpeed: number | null;
    climbRate: number | null;
  };
  battery: {
    percent: number | null;
    voltage: number | null;
    current: number | null;
  };
  gps: {
    fix: GpsFix;
    count: number | null;
    hdop: number | null;
  };
}

/** Guided-action names, PROTOCOL.md §5.1. */
export type CommandAction =
  | "arm"
  | "disarm"
  | "takeoff"
  | "land"
  | "rtl"
  | "gotoLocation"
  | "setFlightMode"
  | "pause";

/**
 * Command request, PROTOCOL.md §5.1 — request/response keyed by `id`,
 * outside the channel/seq envelope (no subscription needed).
 */
export interface Command {
  type: "command";
  id: string;
  vehicleId: number;
  action: CommandAction;
  params: Record<string, string | number | boolean>;
}

/** Exactly one per request, PROTOCOL.md §5.2. */
export interface CommandAck {
  type: "commandAck";
  id: string;
  vehicleId: number;
  status: "accepted" | "rejected";
  /** Human-readable rejection cause; present when rejected. */
  reason?: string;
  /** MAV_RESULT integer when the autopilot answered. */
  mavResult?: number;
}

/** Unsolicited progress, 0..n per request, PROTOCOL.md §5.3. */
export interface CommandProgress {
  type: "commandProgress";
  id: string;
  vehicleId: number;
  /** 0.0–1.0, or null when indeterminate. */
  progress: number | null;
  message?: string;
}

/** Parameter metadata, PROTOCOL.md §6.1. */
export interface ParamMeta {
  type: "int8" | "uint8" | "int16" | "uint16" | "int32" | "uint32" | "float" | "double";
  units: string | null;
  min: number | null;
  max: number | null;
  default: number | null;
  description: string | null;
}

/** Parameter value response, PROTOCOL.md §6.1. */
export interface ParamValue {
  type: "paramValue";
  id: string;
  vehicleId: number;
  path: string;
  value: number;
  meta: ParamMeta;
}

/** Command-lifecycle messages the bridge pushes, keyed by request id. */
export type CommandResponse = CommandAck | CommandProgress;

/**
 * Periodic bridge heartbeat, PROTOCOL.md §11.1 — sent 1 Hz after helloAck,
 * outside the channel/seq envelope (no subscription, no ordering).
 */
export interface Tick {
  type: "tick";
  /** Server clock, µs since epoch — for offset/latency estimation. */
  serverTimeUs: number;
  uptimeS: number;
  /** Vehicle ids the bridge currently knows about (state, per §2.1). */
  vehicleIds: number[];
}

/** Any message the bridge can push to the client. */
export type BridgeMessage = Telemetry | Tick | ParamValue;

/** Session handshake, PROTOCOL.md §1.1 — must be the first client message. */
export interface Hello {
  type: "hello";
  id: string;
  /** Any non-empty token in v0.1. */
  token: string;
  protocolVersion: string;
}

/**
 * Stream subscription request, PROTOCOL.md §2.2. Vehicle-scoped channels key
 * by `vehicleId`; `channel: "video"` keys by `streamId` instead (§9.1). Both
 * fields are optional on one interface (rather than a discriminated union
 * per channel) to keep call sites simple — exactly one is expected, chosen
 * by `channel`.
 *
 * Note: PROTOCOL.md's §9.1 wire example uses a string camId (`"cam1"`) for
 * video `streamId`. This client instead types it as `number` to match the
 * numeric stream index used everywhere else in the video pipeline
 * (`VideoFrameHeader.streamId`, `VideoStreamDecoderOptions.streamId`,
 * `VideoPlayerProps.streamId`, all in src/video/Decoder.ts and
 * src/components/video/VideoPlayer.tsx) — one id shape end to end.
 */
export interface Subscribe {
  type: "subscribe";
  id: string;
  channel: Channel;
  vehicleId?: number;
  /** Video-only (§9.1): the stream to subscribe. See note above re: type. */
  streamId?: number;
}

/** Stream teardown, PROTOCOL.md §2.2. See {@link Subscribe} for the `streamId` note. */
export interface Unsubscribe {
  type: "unsubscribe";
  id: string;
  channel: Channel;
  vehicleId?: number;
  streamId?: number;
}

/** Get parameter request, PROTOCOL.md §6.1. */
export interface GetParam {
  type: "getParam";
  id: string;
  vehicleId: number;
  path: string;
}

/** Set parameter request, PROTOCOL.md §6.2. */
export interface SetParam {
  type: "setParam";
  id: string;
  vehicleId: number;
  path: string;
  value: number;
}

/** Any message the client can send to the bridge. */
export type ClientMessage = Command | Hello | Subscribe | Unsubscribe | GetParam | SetParam;
