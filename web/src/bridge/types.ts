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
export type Channel = "telemetry" | "tick" | "command";

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
    lat: number;
    lon: number;
    altMSL: number;
    altRel: number;
  };
  attitude: {
    roll: number;
    pitch: number;
    /** 0–360, heading. */
    yaw: number;
  };
  velocity: {
    groundSpeed: number;
    /** null on vehicles without an airspeed sensor. */
    airSpeed: number | null;
    climbRate: number;
  };
  battery: {
    percent: number;
    voltage: number;
    current: number;
  };
  gps: {
    fix: GpsFix;
    count: number;
    hdop: number;
  };
}

/** Command sent from the UI to the bridge, targeting one vehicle. */
export interface Command extends VehicleMessage {
  channel: "command";
  /** Command name, e.g. "arm", "disarm", "takeoff", "rtl", "setMode". */
  name: string;
  /** Command-specific parameters (kept open; the bridge validates). */
  params: Record<string, string | number | boolean>;
}

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
export type BridgeMessage = Telemetry | Tick;

/** Session handshake, PROTOCOL.md §1.1 — must be the first client message. */
export interface Hello {
  type: "hello";
  id: string;
  /** Any non-empty token in v0.1. */
  token: string;
  protocolVersion: string;
}

/** Stream subscription request, PROTOCOL.md §2.2. */
export interface Subscribe {
  type: "subscribe";
  id: string;
  channel: Channel;
  vehicleId: number;
}

/** Stream teardown, PROTOCOL.md §2.2. */
export interface Unsubscribe {
  type: "unsubscribe";
  id: string;
  channel: Channel;
  vehicleId: number;
}

/** Any message the client can send to the bridge. */
export type ClientMessage = Command | Hello | Subscribe | Unsubscribe;
