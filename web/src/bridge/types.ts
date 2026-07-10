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
export type Channel = "telemetry" | "tick" | "command" | "video" | "mission" | "image";

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

/** Notification severity — drives toast color and (for warning/critical) speech. */
export type NotificationSeverity = "info" | "warning" | "critical";

/**
 * Unsolicited notification push, PROTOCOL.md §14 — server-push, no
 * subscription, outside the channel/seq envelope like {@link Tick} (§14: "not
 * state in the §2 sense" — a missed notification simply never displays, same
 * as a missed tick). Dispatched by `type`, not by channel. Every
 * hello-authenticated client receives every broadcast notification, plus one
 * targeted welcome notification (severity "info", text "Ghost bridge ready",
 * no vehicleId) right after its own helloAck (§14.3).
 */
export interface Notification {
  type: "notification";
  severity: NotificationSeverity;
  /**
   * Human-readable, already de-jargoned for speech (§14: "the same text QGC
   * would otherwise speak locally") — safe to pass directly to
   * speechSynthesis with no client-side reformatting.
   */
  text: string;
  /** Bridge-side timestamp, microseconds since epoch (matches Tick.serverTimeUs's unit). */
  timeUs: number;
  /**
   * Present only when unambiguously attributable to one vehicle at the
   * source (§14: v0.1 omits it for every current source; multi-vehicle
   * disambiguation, when present, rides in `text` instead). Absence does
   * NOT imply a single-vehicle deployment — never assume that.
   */
  vehicleId?: number;
}

/**
 * Mission item schema, PROTOCOL.md §7.1 — field-for-field with MAVLink
 * `MISSION_ITEM_INT` (coordinates as plain degrees/meters; the bridge handles
 * the `int32 * 1e7` wire conversion). Unlike {@link Telemetry}, mission items
 * are fully specified by the autopilot — no nullable-field discipline here.
 */
export interface MissionItem {
  seq: number;
  /** `MAV_FRAME`, e.g. 6 = `GLOBAL_RELATIVE_ALT_INT`. */
  frame: number;
  /** `MAV_CMD`, e.g. 16 = `NAV_WAYPOINT`, 22 = `NAV_TAKEOFF`. */
  command: number;
  current: boolean;
  autoContinue: boolean;
  param1: number;
  param2: number;
  param3: number;
  param4: number;
  /** Degrees. */
  lat: number;
  /** Degrees. */
  lon: number;
  /** Meters. */
  alt: number;
}

/**
 * Mission state snapshot/update, PROTOCOL.md §7.2 — the on-vehicle mission is
 * state like any other channel; subscribing to `"mission"` yields this
 * stream (full item list + the index the vehicle is currently flying to).
 */
export interface MissionState extends VehicleMessage {
  channel: "mission";
  snapshot: boolean;
  currentSeq: number;
  items: MissionItem[];
}

/** Upload request, PROTOCOL.md §7.2 — replaces the vehicle's mission wholesale. */
export interface MissionUpload {
  type: "missionUpload";
  id: string;
  vehicleId: number;
  items: MissionItem[];
}

/** Download request, PROTOCOL.md §7.2 — answered by {@link MissionItems}. */
export interface MissionDownload {
  type: "missionDownload";
  id: string;
  vehicleId: number;
}

/** Clear request, PROTOCOL.md §7.2 — answered by {@link MissionAck} with `itemCount: 0`. */
export interface MissionClear {
  type: "missionClear";
  id: string;
  vehicleId: number;
}

/**
 * Response to missionUpload/missionClear, PROTOCOL.md §7.2 — request/response
 * keyed by `id`, same shape as {@link CommandAck}.
 */
export interface MissionAck {
  type: "missionAck";
  id: string;
  vehicleId: number;
  status: "accepted" | "rejected";
  itemCount?: number;
  /** Human-readable rejection cause; present when rejected. */
  reason?: string;
  /** MAV_MISSION_RESULT integer when the autopilot answered. */
  mavResult?: number;
}

/** Response to missionDownload, PROTOCOL.md §7.2. */
export interface MissionItems {
  type: "missionItems";
  id: string;
  vehicleId: number;
  items: MissionItem[];
}

/**
 * Mission-lifecycle messages the bridge pushes, keyed by request id — mirrors
 * {@link CommandResponse}; deliberately excluded from {@link BridgeMessage}
 * since they aren't part of the channel/seq snapshot stream.
 */
export type MissionResponse = MissionAck | MissionItems;

/**
 * Wire `format` values for {@link ImageState}, PROTOCOL.md §15 — mapped
 * server-side from the MAVLink DATA_TRANSMISSION_HANDSHAKE's
 * `MAVLINK_DATA_STREAM_IMG_*` type. `jpeg`/`png`/`bmp` are directly
 * renderable via a `data:` URI; `pgm`/`raw8u`/`raw32u` have no browser-native
 * container and need manual decode (see {@link ImageState}'s doc comment).
 */
export type ImageFormat = "jpeg" | "png" | "bmp" | "pgm" | "raw8u" | "raw32u" | "unknown";

/**
 * MAVLink image-transmission-protocol state for one vehicle, PROTOCOL.md
 * §15 (Q8d) — the raw-bytes-before-decode twin of the `video` channel's
 * "server forwards encoded bytes, client decodes" philosophy (§9), but
 * base64-in-JSON rather than a second binary-frame path (see §15.1's
 * rationale: this protocol is ~1 Hz max, so the size/complexity tradeoff
 * that justifies video's binary framing doesn't apply here). Vehicle-scoped
 * and subscribed like {@link Telemetry}/{@link MissionState} — the vehicle's
 * most recently completed image is state, snapshotted on (re)subscribe if
 * one exists yet (§15.2).
 */
export interface ImageState extends VehicleMessage {
  channel: "image";
  /** Monotonic per vehicle (ImageProtocolManager::flowImageIndex()) — independent of `seq`, which resets on every re-subscribe. */
  imageIndex: number;
  format: ImageFormat;
  /** Declared by the source; may be 0 if unknown. */
  width: number;
  height: number;
  /** Base64 of the exact bytes reassembled from ENCAPSULATED_DATA — decode per `format` (§15.3). */
  data: string;
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

/**
 * Setting group names, PROTOCOL.md §16.2 — the whitelist is closed (not an
 * open string) so a typo in a group name is a compile error, not a silent
 * no-op against the wire. Capitalized to match the wire exactly (these ride
 * verbatim as the `group` field, unlike e.g. {@link Channel}).
 */
export type SettingGroup = "Video" | "App" | "AutoConnect";

/** Wire scalar shape a setting's `value` may take, PROTOCOL.md §16.3. */
export type SettingScalar = string | number | boolean;

/**
 * Value-type + validation metadata for one setting, PROTOCOL.md §16.3 —
 * mirrors {@link ParamMeta}'s role for `fact`, but coarser: app settings
 * never carry MAVLink param-storage width like `uint8`/`float`.
 */
export interface SettingMeta {
  type: "string" | "int" | "double" | "bool";
  /** Present only for a setting that has one (currently just `videoSource`, §16.3) — the exact accepted values, sourced live server-side. */
  enumValues?: string[];
}

/** One whitelisted setting's current value, PROTOCOL.md §16.2/§16.3. */
export interface SettingValue {
  group: SettingGroup;
  name: string;
  value: SettingScalar;
  meta: SettingMeta;
}

/** Request settings, PROTOCOL.md §16.3 — `group` omitted returns every whitelisted setting from every group. */
export interface GetSettings {
  type: "getSettings";
  id: string;
  group?: SettingGroup;
}

/**
 * Response to both {@link GetSettings} and {@link SetSetting}, PROTOCOL.md
 * §16.3/§16.4 — one shape for both: `getSettings` answers with every
 * matching setting, `setSetting` answers with `settings` containing exactly
 * the one changed entry (the value read back immediately after the
 * synchronous write, §16.1/§16.4 — never a rejection-by-hardware the way
 * §6.2's `setParam` readback can differ from the requested value).
 */
export interface SettingsValue {
  type: "settingsValue";
  id: string;
  settings: SettingValue[];
}

/** Request to change one whitelisted setting, PROTOCOL.md §16.4. Answered by {@link SettingsValue}, or a keyed §10 error (`SETTING_NOT_ALLOWED`/`VALUE_OUT_OF_RANGE`) on failure. */
export interface SetSetting {
  type: "setSetting";
  id: string;
  group: SettingGroup;
  name: string;
  value: SettingScalar;
}

/**
 * Unsolicited push whenever a whitelisted setting's value changes for any
 * reason (this client's own accepted {@link SetSetting}, another client's,
 * or any other in-process write), PROTOCOL.md §16.5 — server-push, no
 * subscription, no channel/seq/snapshot envelope, dispatched by `type` like
 * {@link Notification}/{@link Tick}. Every accepted `setSetting` fires this
 * too (response first, then this, per §16.4) — so it is the one signal that
 * covers both "my own write landed" and "another client changed it".
 */
export interface SettingChanged {
  type: "settingChanged";
  group: SettingGroup;
  name: string;
  value: SettingScalar;
  meta: SettingMeta;
  /** Bridge-side timestamp, microseconds since epoch (matches Tick.serverTimeUs's unit). */
  timeUs: number;
}

/**
 * Synthetic, client-only translation of a keyed §10 error answering a
 * `getSettings`/`setSetting` (`SETTING_NOT_ALLOWED`, `UNKNOWN_SETTINGS_GROUP`,
 * `VALUE_OUT_OF_RANGE`, or a `BAD_MESSAGE` for a malformed request) — never
 * appears on the wire as this shape. BridgeClient.ts performs this
 * translation for every keyed error (see its §10 handling), the same way it
 * already synthesizes rejected {@link CommandAck}/{@link MissionAck} for
 * command/mission — `settingsValue` itself has no "rejected" variant, so
 * this is how a failed `setSetting` reaches `onSettingsResponse` at all.
 */
export interface SettingsError {
  type: "settingsError";
  id: string;
  code: string;
  message: string;
}

/** Settings-lifecycle messages the bridge (effectively) produces, keyed by request id — mirrors {@link MissionResponse}. */
export type SettingsResponse = SettingsValue | SettingsError;

/**
 * Configured-link transport kind, PROTOCOL.md §16.7 — mirrors
 * `LinkConfiguration::LinkType`; `other` covers any value added to that C++
 * enum after PROTOCOL.md was written.
 */
export type LinkType = "serial" | "udp" | "tcp" | "bluetooth" | "mock" | "logReplay" | "other";

/** The three link types `addLink` can construct end to end, PROTOCOL.md §16.7. */
export type AddableLinkType = "serial" | "udp" | "tcp";

/** `getLinks`/`addLink` `config` shape for a serial link, PROTOCOL.md §16.7's table. */
export interface SerialLinkFields {
  /** Device path, e.g. `/dev/ttyUSB0`. */
  port: string;
  baud: number;
}

/** `getLinks`/`addLink` `config` shape for a udp link, PROTOCOL.md §16.7's table. */
export interface UdpLinkFields {
  listenPort: number;
  /** `"host:port"` strings, `UDPConfiguration::hostList()`. Optional on `addLink` — a listen-only link needs none. */
  targetHosts?: string[];
}

/** `getLinks`/`addLink` `config` shape for a tcp link, PROTOCOL.md §16.7's table. */
export interface TcpLinkFields {
  host: string;
  port: number;
}

/** A configured link's per-type config — `{}` for any `type` outside {@link AddableLinkType} (listing only, §16.7). */
export type LinkFields = SerialLinkFields | UdpLinkFields | TcpLinkFields | Record<string, never>;

/**
 * One configured link, PROTOCOL.md §16.7 — `name` is the primary key (link
 * requests address by name, not a server-assigned id: unlike vehicles/
 * missions there is no numeric identity here). `connected` is a live-ish
 * read (`LinkInterface::isConnected()`), not push-updated — see
 * {@link ConnectLink}'s doc comment on why the client re-polls `getLinks`.
 */
export interface LinkConfig {
  name: string;
  type: LinkType;
  connected: boolean;
  autoConnect: boolean;
  config: LinkFields;
}

/** Request the full configured-links list, PROTOCOL.md §16.7. */
export interface GetLinks {
  type: "getLinks";
  id: string;
}

/** Response to {@link GetLinks}. */
export interface LinksValue {
  type: "linksValue";
  id: string;
  links: LinkConfig[];
}

/**
 * Request to add a new link, PROTOCOL.md §16.7 — `config.name` is required
 * and must be unique (case-sensitive) among existing link names; only
 * `serial`/`udp`/`tcp` are constructible over the wire (the three
 * {@link AddableLinkType}s). Persisted immediately but not connected — call
 * {@link ConnectLink} separately.
 */
export interface AddLink {
  type: "addLink";
  id: string;
  config:
    | ({ type: "serial"; name: string } & Partial<SerialLinkFields>)
    | ({ type: "udp"; name: string } & Partial<UdpLinkFields>)
    | ({ type: "tcp"; name: string } & Partial<TcpLinkFields>);
}

/** Request to remove a configured link by name, PROTOCOL.md §16.7 — disconnects it first if currently connected. */
export interface RemoveLink {
  type: "removeLink";
  id: string;
  name: string;
}

/**
 * Request to connect a configured link by name, PROTOCOL.md §16.7.
 * **Fire-and-forget, not synchronous**: `status: "accepted"` on the
 * resulting {@link LinkAck} means "the connect was requested", not "the
 * link is now connected" (the actual socket connect happens on the link's
 * own worker thread). There is no `linksChanged` broadcast (v0.1) — observe
 * the transition via a follow-up {@link GetLinks}.
 */
export interface ConnectLink {
  type: "connectLink";
  id: string;
  name: string;
}

/** Request to disconnect a configured link by name, PROTOCOL.md §16.7 — same fire-and-forget caveat as {@link ConnectLink}. */
export interface DisconnectLink {
  type: "disconnectLink";
  id: string;
  name: string;
}

/**
 * Response to addLink/removeLink/connectLink/disconnectLink, PROTOCOL.md
 * §16.7 — request/response keyed by `id`. Never carries the resulting
 * {@link LinkConfig} (unlike e.g. {@link MissionAck}'s `itemCount`) — a
 * link name naming no existing configuration is `status: "rejected"` here
 * (not a §10 error, since the request's *addressing* is what's wrong, not
 * its envelope shape — contrast §3.1's `UNKNOWN_VEHICLE`).
 */
export interface LinkAck {
  type: "linkAck";
  id: string;
  status: "accepted" | "rejected";
  name: string;
  reason?: string;
}

/** Link-lifecycle messages the bridge pushes, keyed by request id — mirrors {@link SettingsResponse}. */
export type LinksResponse = LinksValue | LinkAck;

/** Any message the bridge can push to the client. */
export type BridgeMessage = Telemetry | Tick | ParamValue | MissionState | ImageState;

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
export type ClientMessage =
  | Command
  | Hello
  | Subscribe
  | Unsubscribe
  | GetParam
  | SetParam
  | MissionUpload
  | MissionDownload
  | MissionClear
  | GetSettings
  | SetSetting
  | GetLinks
  | AddLink
  | RemoveLink
  | ConnectLink
  | DisconnectLink;
