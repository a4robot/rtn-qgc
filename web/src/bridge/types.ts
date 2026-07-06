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

/** Full telemetry state snapshot for one vehicle. */
export interface Telemetry extends VehicleMessage {
  channel: "telemetry";
  /** Whether the vehicle link is currently alive. */
  connected: boolean;
  armed: boolean;
  flightMode: string;
  position: {
    latitudeDeg: number;
    longitudeDeg: number;
    altitudeMslM: number;
    altitudeRelM: number;
  };
  attitude: {
    rollDeg: number;
    pitchDeg: number;
    yawDeg: number;
  };
  velocity: {
    groundSpeedMps: number;
    airSpeedMps: number;
    climbRateMps: number;
  };
  battery: {
    voltageV: number;
    remainingPct: number;
  };
  gps: {
    fixType: number;
    satelliteCount: number;
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

/** Periodic bridge heartbeat; also carries per-channel head seq numbers. */
export interface Tick extends ChannelMessage {
  channel: "tick";
  /** Latest seq the bridge has published per channel (gap-recovery aid). */
  channelHeads: Partial<Record<Channel, number>>;
  /** Vehicle ids the bridge currently knows about. */
  vehicleIds: number[];
}

/** Any message the bridge can push to the client. */
export type BridgeMessage = Telemetry | Tick;

/** Any message the client can send to the bridge. */
export type ClientMessage = Command;
