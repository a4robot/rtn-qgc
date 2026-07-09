# QGC WebBridge Protocol

**Version: v0.1 draft** — Strangler-fig port, job B0.
This document is the contract between the C++ bridge (`src/WebBridge/`) and the Web client. It is normative: a mock server built from this spec alone must be indistinguishable from the real bridge for the message set it implements.

---

## 1. Transport

| Property | Value |
|---|---|
| Transport | WebSocket (RFC 6455) |
| Endpoint | `ws://127.0.0.1:8877/` (localhost only; default port **8877**, configurable) |
| Control / state | **Text frames**, one JSON object per frame (UTF-8, no framing beyond WS) |
| Video | **Binary frames** (see §9) |
| Subprotocol | none (reserved) |

- One WebSocket connection carries all channels (multiplexed by `channel` / `type`).
- The server never fragments a JSON message across frames; one frame == one message.
- Numbers are IEEE-754 doubles unless a field is documented as integer. Timestamps are microseconds since Unix epoch (`u64`, transmitted as JSON number — safe below 2^53).

### 1.1 Authentication (hello)

The **first** message on a connection MUST be `hello`. Any other first message → error `AUTH_REQUIRED` and the server closes the socket (WS close code 1008).

```json
{ "type": "hello", "token": "dev-token", "protocolVersion": "0.1", "client": "qgc-web/0.1.0" }
```

Server response:

```json
{ "type": "helloAck", "protocolVersion": "0.1", "serverVersion": "rtn-qgc 5.0", "serverTimeUs": 1767690000000000 }
```

> **Token mechanism is TBD (M6).** Until then the field is required but its value is not validated — the mock and the M1–M5 bridge MUST accept any non-empty string.

If `protocolVersion` major version is unsupported, server replies with error `UNSUPPORTED_VERSION` and closes.

> **`--bridge-token` (bridge security hardening).** The C++ bridge accepts an optional `--bridge-token <token>` CLI flag. When set, `hello.token` is checked against it exactly; a mismatch (or missing token) replies error `AUTH_FAILED` and the server closes the connection (WS close code 1008), same as `AUTH_REQUIRED`. When `--bridge-token` is **not** set, the field stays unvalidated as above (any non-empty string accepted) — this is the default and matches the mock server's behavior. The bridge also binds `127.0.0.1` (localhost only) by default regardless of token configuration; `--bridge-host <addr>` opts into a wider bind (e.g. `0.0.0.0` for LAN use) and is logged as a warning, since the protocol has no transport encryption of its own.

---

## 2. Principles (normative)

1. **State, not events.** The server publishes *current state* — full snapshots or patches whose fields are absolute values (merge semantics). It never sends deltas that a client must accumulate to reconstruct state (no "battery dropped 2%", no "position moved by dx"). A client that misses N messages and receives message N+1 has correct (if stale-then-fresh) state.
2. **Snapshot on connect/subscribe.** On every successful `subscribe`, the server immediately sends one full-state snapshot for that channel (`"snapshot": true`), then streams updates. Subscribing is idempotent; re-subscribing re-sends the snapshot.
3. **Vehicle scoping.** Every vehicle-scoped message (telemetry, command, fact, mission — both directions) carries an integer `vehicleId` (MAVLink system id). Non-vehicle channels (`video`, `adsb`, `tick`) omit it.
4. **Sequence numbers & gap detection.** Every server→client message on a subscribed channel carries `seq`: a `u32` that is monotonic and contiguous **per (channel, vehicleId) stream**, starting at `1` with the snapshot. On detecting a gap (`seq != last + 1`), the client MUST re-`subscribe` to that stream; the server responds with a fresh snapshot (whose `seq` restarts at 1). Clients never attempt to repair gaps by inference.

---

## 3. Message envelope

### Client → server

```json
{ "type": "<messageType>", "id": "<clientRequestId?>", "vehicleId": 1, "...": "payload fields" }
```

| Field | Type | Required | Notes |
|---|---|---|---|
| `type` | string | yes | Message type (tables below) |
| `id` | string | for request/response types | Client-generated, unique per connection; echoed in the response |
| `vehicleId` | int | vehicle-scoped messages | MAVLink system id |

### Server → client

| Field | Type | Required | Notes |
|---|---|---|---|
| `type` | string | yes | |
| `channel` | string | stream messages | `telemetry`, `mission`, `video`, `adsb` |
| `vehicleId` | int | vehicle-scoped | |
| `seq` | u32 | stream messages | Per-stream, contiguous (§2.4) |
| `snapshot` | bool | stream messages | `true` on the first message after subscribe |
| `id` | string | responses | Echo of the request `id` |
| `timeUs` | u64 | stream messages | Server timestamp, microseconds |

### 3.1 Subscribe / unsubscribe

```json
{ "type": "subscribe",   "id": "s1", "channel": "telemetry", "vehicleId": 1 }
{ "type": "unsubscribe", "id": "s2", "channel": "telemetry", "vehicleId": 1 }
```

Ack: `{ "type": "subscribeAck", "id": "s1", "channel": "telemetry", "vehicleId": 1 }` followed immediately by the snapshot (if the vehicle is already known -- see below). Unknown channel → error `UNKNOWN_CHANNEL`; a vehicle-scoped channel (`telemetry`, `mission`) with `vehicleId` omitted entirely → error `BAD_MESSAGE` (§10: "missing required field" — §3's envelope table marks `vehicleId` required for vehicle-scoped messages).

Subscribable channels: `telemetry`, `mission`, `image` (§15), `video` (uses `streamId` instead of `vehicleId`, §9), `adsb`.

> **Late-binding subscribes (v0.1 clarification).** A `subscribe` naming a `vehicleId` that is *not currently* in the tracked vehicle set (i.e. not in the most recent `tick.vehicleIds`, §11.1) is **not** an error: it is `subscribeAck`'d exactly like a known vehicle, and simply produces no snapshot yet — the client is now durably subscribed to that (channel, vehicleId) stream and starts receiving state transparently the moment the vehicle appears (no re-subscribe needed), same as if it had subscribed after the vehicle connected. This is deliberate, not a bug: the web client's own startup handshake (`web/src/bridge/session.ts`) subscribes `telemetry`/`mission` for its configured vehicle immediately after `hello`, routinely *before* a MockLink or real vehicle has finished attaching (autopilot boot/handshake can take ~1 s or more) — rejecting that subscribe with `UNKNOWN_VEHICLE` would break every cold start unless the client added its own "wait for tick, then subscribe" gate, which v0.1 does not require. A caveat of this: because delivery isn't gated by an explicit re-subscribe, the *first* message a late-bound client actually receives is not guaranteed to carry `snapshot: true` / `seq: 1` — it is instead whatever the channel's next periodic publish produces for that vehicle (still a **complete** current-state message per §2's "state, not events", just not flagged as the stream's first message). A client that cares about the seq/snapshot invariant for a vehicle it isn't sure exists yet should watch `tick.vehicleIds` and only subscribe (or re-subscribe, to force a fresh seq-1 snapshot) once the id appears there.
>
> An `UNKNOWN_VEHICLE` error remains reserved for vehicle-scoped **request/response** messages answered against a specific `vehicleId` at the moment they are handled — `command` (§5), `getParam`/`setParam` (§6), and `missionUpload`/`missionDownload`/`missionClear` (§7.2) all reply with the §10 error envelope (not their usual ack shape) when `vehicleId` does not resolve to a tracked vehicle *right now*, since (unlike a subscribe) there is no later moment at which that specific request could still be answered.

---

## 4. Channel: `telemetry`

Server → client only. Rate: **~10 Hz** while subscribed (plus the initial snapshot). Every message is a **full snapshot** of the fields below (patch messages are reserved for a later version; v0.1 always sends the complete object). Fields whose value is unknown are `null`, never omitted.

| Group | Field | Type | Units |
|---|---|---|---|
| `attitude` | `roll`, `pitch`, `yaw` | double | degrees (yaw 0–360, heading) |
| `position` | `lat`, `lon` | double | degrees WGS-84 |
| | `altMSL`, `altRel` | double | meters |
| `velocity` | `groundSpeed`, `airSpeed`, `climbRate` | double | m/s |
| `battery` | `percent` | double | 0–100 |
| | `voltage` | double | volts |
| | `current` | double | amps |
| `gps` | `fix` | string | `none` \| `2d` \| `3d` \| `rtkFloat` \| `rtkFixed` |
| | `count` | int | satellites used |
| | `hdop` | double | dimensionless |
| top-level | `flightMode` | string | Firmware mode name, e.g. `"Hold"`, `"Mission"` |
| | `armed` | bool | |

```json
{
  "type": "telemetry", "channel": "telemetry", "vehicleId": 1,
  "seq": 42, "snapshot": false, "timeUs": 1767690001234567,
  "attitude": { "roll": -2.3, "pitch": 5.1, "yaw": 271.4 },
  "position": { "lat": 13.7367, "lon": 100.5232, "altMSL": 58.2, "altRel": 25.0 },
  "velocity": { "groundSpeed": 7.4, "airSpeed": null, "climbRate": -0.2 },
  "battery":  { "percent": 81.0, "voltage": 24.9, "current": 12.3 },
  "gps":      { "fix": "3d", "count": 14, "hdop": 0.8 },
  "flightMode": "Mission",
  "armed": true
}
```

---

## 5. Channel: `command`

Request/response plus unsolicited progress. No subscription needed — sending a `command` implicitly opts you into its ack/progress/result messages, all keyed by the request `id`.

### 5.1 Request

```json
{ "type": "command", "id": "c-17", "vehicleId": 1, "action": "takeoff", "params": { "alt": 20.0 } }
```

| `action` | `params` | Notes |
|---|---|---|
| `arm` | `{}` | |
| `disarm` | `{}` | |
| `takeoff` | `{ "alt": <m, relative> }` | |
| `land` | `{}` | Land at current position |
| `rtl` | `{}` | Return to launch |
| `gotoLocation` | `{ "lat": <deg>, "lon": <deg>, "alt": <m, relative> }` | Guided reposition |
| `setFlightMode` | `{ "mode": "<firmware mode name>" }` | Same namespace as telemetry `flightMode` |
| `pause` | `{}` | Pause mission / hold position |

### 5.2 Response (exactly one per request)

Every response carries `status` (`accepted` | `rejected`), a human-readable `reason` when rejected, and `mavResult` (integer `MAV_RESULT` value) when the autopilot answered.

```json
{ "type": "commandAck", "id": "c-17", "vehicleId": 1, "status": "accepted", "mavResult": 0 }
```

```json
{ "type": "commandAck", "id": "c-18", "vehicleId": 1, "status": "rejected",
  "reason": "Vehicle not armable: GPS fix required", "mavResult": 4 }
```

`accepted` means the autopilot accepted the command, not that the action completed — track completion via telemetry (`armed`, `flightMode`, `altRel`) and progress messages.

A `command` naming a `vehicleId` that does not resolve to a currently-tracked vehicle is answered with the §10 error envelope (`UNKNOWN_VEHICLE`), not a `commandAck` — see §3.1's late-binding note for why this differs from `subscribe`'s treatment of the same condition.

### 5.3 Progress (unsolicited, 0..n per request)

```json
{ "type": "commandProgress", "id": "c-17", "vehicleId": 1, "progress": 0.55, "message": "Climbing to 20 m" }
```

`progress` is 0.0–1.0 or `null` when indeterminate. Progress messages stop after `commandAck` with `rejected` or once the action completes.

---

## 6. Channel: `fact` (parameters)

Request/response. Parameter paths are strings: `"vehicle.<vehicleId>.<PARAM_NAME>"`, e.g. `"vehicle.1.MPC_XY_VEL_MAX"`. `vehicleId` in the envelope MUST match the path.

### 6.1 getParam

```json
{ "type": "getParam", "id": "p-1", "vehicleId": 1, "path": "vehicle.1.MPC_XY_VEL_MAX" }
```

```json
{
  "type": "paramValue", "id": "p-1", "vehicleId": 1,
  "path": "vehicle.1.MPC_XY_VEL_MAX",
  "value": 12.0,
  "meta": {
    "type": "float",
    "units": "m/s",
    "min": 0.0, "max": 20.0,
    "default": 12.0,
    "description": "Maximum horizontal velocity"
  }
}
```

`meta.type` ∈ `int8|uint8|int16|uint16|int32|uint32|float|double`. `min`/`max`/`units`/`default`/`description` are `null` when metadata is unavailable.

### 6.2 setParam

```json
{ "type": "setParam", "id": "p-2", "vehicleId": 1, "path": "vehicle.1.MPC_XY_VEL_MAX", "value": 10.0 }
```

Response is a `paramValue` (same shape as above, echoing `id`) carrying the **readback** value confirmed by the vehicle — which is the new state, per principle §2.1. Failure (timeout, out of range, unknown param) → error envelope (§10) with the request `id`.

---

## 7. Channel: `mission`

### 7.1 Item schema

Aligned field-for-field with MAVLink `MISSION_ITEM_INT`; coordinates are expressed in degrees (double) and the bridge converts to/from the `int32 * 1e7` wire encoding losslessly at 7 decimal places.

| Field | Type | MISSION_ITEM_INT equivalent |
|---|---|---|
| `seq` | int | `seq` |
| `frame` | int | `frame` (`MAV_FRAME`, e.g. 6 = `GLOBAL_RELATIVE_ALT_INT`) |
| `command` | int | `command` (`MAV_CMD`, e.g. 16 = `NAV_WAYPOINT`) |
| `current` | bool | `current` |
| `autoContinue` | bool | `autocontinue` |
| `param1`..`param4` | double | `param1`..`param4` |
| `lat` | double (deg) | `x / 1e7` |
| `lon` | double (deg) | `y / 1e7` |
| `alt` | double (m) | `z` |

### 7.2 Operations

```json
{ "type": "missionUpload", "id": "m-1", "vehicleId": 1, "items": [
  { "seq": 0, "frame": 6, "command": 22, "current": true,  "autoContinue": true,
    "param1": 0, "param2": 0, "param3": 0, "param4": 0, "lat": 13.7367, "lon": 100.5232, "alt": 20.0 },
  { "seq": 1, "frame": 6, "command": 16, "current": false, "autoContinue": true,
    "param1": 0, "param2": 0, "param3": 0, "param4": 0, "lat": 13.7400, "lon": 100.5300, "alt": 30.0 }
] }
```

```json
{ "type": "missionDownload", "id": "m-2", "vehicleId": 1 }
{ "type": "missionClear",    "id": "m-3", "vehicleId": 1 }
```

Responses:

```json
{ "type": "missionAck", "id": "m-1", "vehicleId": 1, "status": "accepted", "itemCount": 2 }
{ "type": "missionItems", "id": "m-2", "vehicleId": 1, "items": [ /* item schema above */ ] }
{ "type": "missionAck", "id": "m-3", "vehicleId": 1, "status": "accepted", "itemCount": 0 }
```

Failures use `status: "rejected"` + `reason` (+ `mavResult`/`MAV_MISSION_RESULT` in `mavResult` when available). A mission message naming a `vehicleId` that does not resolve to a currently-tracked vehicle is answered with the §10 error envelope (`UNKNOWN_VEHICLE`) instead of a `missionAck`/`missionItems` — same reasoning as §5.2's `command` note. Subscribing to channel `mission` yields a `missionState` snapshot/update stream (current mission `items`, `currentSeq`) following §2 — the on-vehicle mission is state like any other.

> **`itemCount` may be less than the uploaded array length.** A successful `missionUpload`'s `missionAck.itemCount` reflects the vehicle's *post-write* mission item count, not `items.length` from the request — `PlanManager::writeMissionItems()` silently drops the first (home) item before sending when the firmware doesn't want it sent as part of the mission proper (`MISSION_TYPE_MISSION`'s home-item convention). A client that uploads N items should not assert `itemCount === N`; asserting `itemCount >= 1` (or comparing against a separately-tracked "did this include a home item" expectation) is the correct check.

```json
{ "type": "missionState", "channel": "mission", "vehicleId": 1, "seq": 3, "snapshot": false,
  "timeUs": 1767690002000000, "currentSeq": 1, "items": [ /* full item list */ ] }
```

---

## 8. Channel: `adsb`

Server → client, subscribed. Each message is the **complete current traffic picture** (snapshot semantics — vanished aircraft simply stop appearing in the array). Rate: on change, max 1 Hz. Not vehicle-scoped.

```json
{
  "type": "adsb", "channel": "adsb", "seq": 12, "snapshot": false, "timeUs": 1767690003000000,
  "traffic": [
    { "icao": "48415A", "callsign": "THA123", "lat": 13.70, "lon": 100.60,
      "altMSL": 1200.0, "heading": 95.0, "groundSpeed": 120.0, "climbRate": 2.0, "alert": false }
  ]
}
```

---

## 9. Channel: `video`

### 9.1 Subscribe

Video streams are identified by a numeric `streamId` (u8 range): `1`, `2`, ... — the
same id carried in every binary frame header (§9.2), so one identifier shape flows
end-to-end. *(v0.1 revision: an earlier draft used string cam ids for subscribe and a
separate numeric `streamIndex`; the split identifier bought nothing and confused
implementations.)*

```json
{ "type": "subscribe", "id": "v-1", "channel": "video", "streamId": 1 }
```

On subscribe (and again whenever SPS/PPS change mid-stream), the server sends a JSON codec-config message **before** any binary frame that depends on it:

```json
{
  "type": "videoConfig", "channel": "video", "streamId": 1, "seq": 1, "snapshot": true,
  "timeUs": 1767690004000000,
  "codec": "h264",
  "width": 1920, "height": 1080,
  "sps": "Z2QAKKzZQHgCJ+XARAAAAwAEAAADAPI8YMZY",
  "pps": "aOvssiw="
}
```

`sps`/`pps` are base64 of the raw NAL units (no Annex-B start codes).

### 9.2 Binary frame layout

Each binary WS frame carries exactly one H.264 access unit prefixed by a fixed **16-byte header**. All multi-byte fields are **little-endian**.

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 2 | `magic` | `u16` = `0x4656` (bytes on the wire: `0x56 0x46`, ASCII "VF") |
| 2 | 1 | `version` | `u8` = `1` |
| 3 | 1 | `streamId` | `u8` — same id used in subscribe/videoConfig |
| 4 | 1 | `flags` | `u8` — bit 0: **keyframe** (IDR); bits 1–7 reserved (0) |
| 5 | 3 | `reserved` | zero-filled |
| 8 | 8 | `timestampUs` | `u64` — capture/presentation time, microseconds |
| 16 | n | payload | One complete **Annex-B H.264 access unit** (start codes included) |

Rules: the first binary frame after `videoConfig` is a keyframe; a client joining mid-stream renders nothing until the first keyframe; frames with bad magic/version are dropped and counted, not fatal. Binary frames carry no `seq` — video is inherently lossy state; recovery is "wait for next keyframe", and stream (re)configuration arrives via `videoConfig` on the JSON side.

This is a per-client, server-enforced guarantee, not just client-side advice: the bridge withholds binary frames from each client individually from the moment it (re)subscribes to a `video` stream until the next keyframe for that stream arrives, then forwards normally — a client that subscribes mid-GOP never sees a non-keyframe first frame, regardless of what other already-synced clients on the same stream are receiving concurrently.

---

## 10. Error envelope

All protocol-level failures use one shape. When the error answers a request, `id` echoes it; unsolicited errors omit `id`.

```json
{ "type": "error", "id": "p-2", "code": "PARAM_TIMEOUT",
  "message": "Vehicle did not confirm MPC_XY_VEL_MAX within 3 s",
  "vehicleId": 1, "retryable": true }
```

| `code` | Meaning |
|---|---|
| `AUTH_REQUIRED` | First message was not `hello` (connection closed) |
| `AUTH_FAILED` | Token rejected (M6+; connection closed) |
| `UNSUPPORTED_VERSION` | Protocol major version mismatch (connection closed) |
| `BAD_MESSAGE` | Unparseable JSON / missing required field |
| `UNKNOWN_TYPE` | Unrecognized `type` |
| `UNKNOWN_CHANNEL` | `subscribe` to a channel that does not exist |
| `UNKNOWN_VEHICLE` | `vehicleId` not connected |
| `UNKNOWN_STREAM` | Video `streamId` not available |
| `UNKNOWN_PARAM` | Fact path does not resolve |
| `PARAM_TIMEOUT` | Vehicle did not confirm get/set |
| `VALUE_OUT_OF_RANGE` | setParam value outside `meta.min..max` |
| `COMMAND_FAILED` | Internal failure sending a command (distinct from a vehicle *rejection*, which is a `commandAck`) |
| `INTERNAL` | Bridge-side error |

`retryable` hints whether the identical request may succeed later.

---

## 11. Heartbeat, latency, reconnect

### 11.1 Tick

After `helloAck`, the server sends `tick` at **1 Hz** unconditionally (no subscription):

```json
{ "type": "tick", "serverTimeUs": 1767690005000000, "uptimeS": 3600, "vehicleIds": [1] }
```

- `serverTimeUs` lets the client measure clock offset / latency (compare against local receipt time; smooth over several ticks).
- `vehicleIds` is the current set of connected vehicles — state, per §2.1. A vehicle disappearing from this list means its streams are dead; the client tears down or greys out accordingly.
- **Liveness rule:** if a client receives no message of any kind for **5 s**, it MUST consider the connection dead and reconnect. The server likewise drops clients that fail WS-level pings for 10 s.

### 11.2 Reconnect semantics

1. WS drops (or liveness timeout) → client reconnects with exponential backoff (0.5 s initial, ×2, cap 10 s, jitter recommended).
2. On reconnect: `hello` → `helloAck` → re-`subscribe` every channel it wants. **The server keeps no per-client state across connections**; subscriptions do not survive reconnect.
3. Each re-subscribe yields a fresh snapshot (`seq` restarts at 1), so the client is consistent immediately — no replay, no catch-up (principle §2.1/§2.2).
4. In-flight requests (`command`, `getParam`, ...) at disconnect time are lost; the client re-issues with **new** `id`s and must treat side-effectful commands (`arm`, `takeoff`) as possibly-already-executed — check telemetry state before re-sending.

---

## 12. Mock conformance (M1)

A minimal M1 mock server MUST implement exactly this set, byte-compatible with the schemas above:

| # | Behavior |
|---|---|
| 1 | Accept WS connections on `127.0.0.1:8877` |
| 2 | Require `hello` first; accept any non-empty `token`; reply `helloAck`. Non-`hello` first message → `error AUTH_REQUIRED` + close 1008 |
| 3 | Send `tick` at 1 Hz with real `serverTimeUs` and `vehicleIds: [1]` |
| 4 | Handle `subscribe` for `channel: "telemetry"`, `vehicleId: 1` → `subscribeAck` + one full `telemetry` snapshot (`seq: 1`, `snapshot: true`) |
| 5 | Stream `telemetry` at 10 Hz with contiguous `seq`, plausible varying values (all §4 fields present, `null` allowed for `airSpeed`) |
| 6 | Handle `unsubscribe` (stop the stream) and repeated `subscribe` (restart `seq` at 1 with a snapshot) |
| 7 | Reply `error UNKNOWN_CHANNEL` / `UNKNOWN_VEHICLE` / `UNKNOWN_TYPE` / `BAD_MESSAGE` for the corresponding bad inputs (connection stays open) |

Everything else (`command`, `fact`, `mission`, `video`, `adsb`) is out of scope for M1 and a conforming M1 mock replies `error UNKNOWN_TYPE` (or `UNKNOWN_CHANNEL` for subscribes) to those messages.

---

## 13. Versioning

- Protocol version is `major.minor`. Minor bumps are additive only (new optional fields, new types, new channels); clients MUST ignore unknown fields and unknown message types. Major bumps may break; negotiated at `hello`.
- This document: **v0.1 draft** — subject to change until M1 mock and first client land; changes after that require bumping the version and noting them in a changelog section here.

---

## 14. Channel: `notification`

Server → client only, no subscription -- like `tick` (§11.1), not like the subscribed streams
(`telemetry`, `mission`, `video`, `adsb`): every `hello`-authenticated client receives every
`notification` message, broadcast unconditionally, plus one targeted welcome message right after
its own `helloAck` (§14.3). There is no `channel`/`seq`/`snapshot` envelope (§3) -- a notification
is not "state" in the §2 sense (there is nothing to reconcile after a gap; a missed notification
simply never displays, same as a missed `tick`), so it reuses `tick`'s envelope-free shape rather
than `makeStreamMessage()`'s.

```json
{ "type": "notification", "severity": "info", "text": "Vehicle 1 armed", "timeUs": 1767690006000000, "vehicleId": 1 }
```

| Field | Type | Required | Notes |
|---|---|---|---|
| `type` | string | yes | always `"notification"` |
| `severity` | string | yes | `"info"` \| `"warning"` \| `"critical"` |
| `text` | string | yes | Human-readable announcement text -- the same text QGC would otherwise speak locally (already de-abbreviated/de-jargoned for speech, e.g. "RTL" -> "return to launch"). The browser owns audio in this architecture (QGC_ENABLE_TEXTTOSPEECH gating, prior wave): a client that wants speech runs this through its own TTS. |
| `timeUs` | u64 | yes | Server timestamp, microseconds (§1) |
| `vehicleId` | int | omitted when unknown | Present only when the source event is unambiguously attributable to one vehicle at the C++ signal boundary. v0.1 omits it for every current source (§14.1: none of the three hooked call sites carry a vehicle id at the point of the hook) -- multi-vehicle disambiguation, when present, is embedded in `text` itself instead (e.g. "Vehicle 2 communication lost"). A future wave that re-sources this from `Vehicle`-scoped signals directly may start populating it; clients MUST NOT assume its absence means single-vehicle. |

### 14.1 Sourcing

Three C++ signals feed this one wire channel (`src/WebBridge/NotificationChannel.{h,cc}`):

| Source | Severity | Notes |
|---|---|---|
| `AudioOutput::say()` (every spoken/would-be-spoken vehicle announcement: arm/disarm, flight mode changes, battery warnings, geofence breach, link lost/regained, `STATUSTEXT` messages QGC reads aloud) | `info` or `warning` | Classified by a small case-insensitive keyword scan over `text` (`warning`, `lost`, `breach`, `fail`, `reject`, `error`, `critical`, `emergency` -> `warning`; else `info`). `AudioOutput::say()` fires this signal unconditionally -- regardless of local TTS engine availability, mute, or volume, and identically whether or not the build has `QGC_ENABLE_TEXTTOSPEECH` -- because the browser, not local Qt, owns audio in this architecture; a muted/TTS-less desktop ghost must not silence the web client. |
| `QGCApplication::showCriticalVehicleMessage()` | always `critical` | PreArm/preflight messages are filtered out before the signal fires, same as the desktop toolbar path. This is also the fix for headless boot: previously this method was a pure log statement with no observable side effect when no root QML window exists (`_showErrorsInToolbar` stays false, see `_initForHeadlessBoot()`); it now always fans out here regardless. |
| `QGCApplication::showAppMessage()` | `info` or `warning` | Same keyword classification as `say()`. No double-fire risk: neither `showAppMessage()` nor `showCriticalVehicleMessage()` calls `AudioOutput::say()` (verified by reading both call sites), so each source event reaches the channel exactly once. |

### 14.2 Dedup

Identical `text` repeated within 1 s of its own previous emission is suppressed entirely (not
queued, not coalesced -- simply not sent). This is a per-text debounce, not a global rate limit:
unrelated text is never delayed by it. It exists because `MockLink`/`STATUSTEXT` sources can spam
the identical message every tick (e.g. a repeated pre-arm health-check failure); collapsing that
to at most 1/s keeps the channel useful without QGC-side rate-limiting logic living on the wire
client. The mock server is not required to reproduce this byte-for-byte, but should avoid spamming
duplicate notifications either.

### 14.3 Welcome notification

Immediately after `helloAck`, the server sends that client -- only that client, not a broadcast --
one `notification` with `severity: "info"`, `text: "Ghost bridge ready"`, no `vehicleId`. This is a
deliberate, always-present proof of channel liveness: a client can verify end-to-end notification
delivery without depending on an organic vehicle event (arm, warning, link loss, ...) ever
occurring during its session. It is exempt from §14.2's dedup window (each new connection gets its
own delivery, even if the previous connection's welcome text is technically "the same text" within
the last second of a rapid reconnect).

---

## 15. Channel: `image`

Server → client, vehicle-scoped, subscribed. Carries the MAVLink image transmission protocol
(https://mavlink.io/en/services/image_transmission.html -- `DATA_TRANSMISSION_HANDSHAKE` +
`ENCAPSULATED_DATA`, mainly used by optical flow cameras), added in Q8d
(STRANGLER_MILESTONES.md M8) to restore image capability the QML-OFF ghost lost when Q8d's
predecessor (Q7d) gated the QImage-only decode path out of headless builds to drop `libQt6Gui`.
Same philosophy as `video` (§9): **the server forwards the already-reassembled, still-encoded
bytes; the client decodes.** `src/MAVLink/ImageProtocolManager.{h,cc}` reassembles
`ENCAPSULATED_DATA` packets exactly as before -- this channel taps that reassembly at the same
point the (QML-only) `QImage` decode used to happen, before any decode occurs.

### 15.1 Wire format: base64-in-JSON, not a binary frame (rationale)

Unlike `video` (§9.2's 16-byte binary header + raw Annex-B payload, chosen because H.264 runs at
15-30 fps and both the per-frame header overhead and a base64 size penalty would be wasteful at
that rate), this channel reuses the plain JSON stream envelope (§3) with the image bytes as a
base64 string field. The MAVLink image transmission protocol is inherently low-rate (**~1 Hz
max** -- optical flow cameras and similar sources, not a video feed), so base64's ~33% size
overhead is immaterial in absolute terms, and reusing §3's envelope avoids standing up a second
binary-frame/per-client-keyframe-gate mechanism (§9.2's `WebBridgeServer::broadcastBinary()` +
`ClientState::videoPendingKeyframe`) for a channel that has no GOP/keyframe concept to begin with
-- every image is already a complete, independently-decodable unit, so there is nothing analogous
to "wait for the next keyframe" to implement. video earns its binary-framing complexity at 30x
this channel's rate; `image` does not need to pay for it.

### 15.2 Subscription model

Vehicle-scoped and subscribed exactly like `telemetry`/`mission` (§2/§3.1) -- **not** an
unsubscribed broadcast like `notification` (§14). Rationale: an image originates from one
vehicle's `ImageProtocolManager` and only makes sense addressed to that `vehicleId` (unlike
`notification`, which is a text announcement stream with no inherent per-vehicle *state* to
snapshot); and per principle §2.1 ("state, not events"), a vehicle's most-recently-received image
**is** current state for that data source, the same way telemetry's most recent attitude is --
so, like `telemetry`/`mission`, subscribing yields an immediate snapshot when one exists.

```json
{ "type": "subscribe", "id": "i-1", "channel": "image", "vehicleId": 1 }
```

Ack: `{ "type": "subscribeAck", "id": "i-1", "channel": "image", "vehicleId": 1 }`. If this
vehicle has not yet completed an image transmission, **no snapshot follows** -- there is nothing
to replay yet, the same "nothing cached" case §9.1 describes for `video`'s `videoConfig` before
the first keyframe. The client simply receives the next `image` message whenever
`ImageProtocolManager` completes one.

### 15.3 Message

```json
{
  "type": "image", "channel": "image", "vehicleId": 1,
  "seq": 1, "snapshot": true, "timeUs": 1767690007000000,
  "imageIndex": 3,
  "format": "jpeg",
  "width": 640, "height": 480,
  "data": "<base64 of the exact bytes ImageProtocolManager reassembled from ENCAPSULATED_DATA>"
}
```

| Field | Type | Notes |
|---|---|---|
| `imageIndex` | u32 | `ImageProtocolManager::flowImageIndex()` after this image completed -- monotonic per vehicle, independent of `seq` (which resets to 1 on every re-subscribe per §2.4; `imageIndex` does not). |
| `format` | string | One of `jpeg` \| `png` \| `bmp` \| `pgm` \| `raw8u` \| `raw32u` \| `unknown`, mapped from the preceding `DATA_TRANSMISSION_HANDSHAKE`'s `MAVLINK_DATA_STREAM_IMG_*` type. |
| `width`, `height` | int | The handshake's declared dimensions. May be `0` if the source did not provide them. |
| `data` | string | Base64 of the exact reassembled bytes -- byte-identical to what `QImage::loadFromData()` would have consumed on a QML build before Q8d (§15's raw-bytes-before-decode guarantee). |

**Client decode guidance (informative, not normative):** `jpeg`/`png`/`bmp` are directly
renderable via `<img src="data:image/{format};base64,...">` (browsers accept `image/bmp`).
`pgm`/`raw8u`/`raw32u` carry no browser-native container format -- `raw8u` is one grayscale byte
per pixel, row-major, `width*height` bytes total; a client wanting to display those must decode
manually (e.g. onto a `<canvas>` via `ImageData`) using the `width`/`height` fields. This mirrors
the desktop QML build's own `_getImage()`, which synthesizes a PGM header for `raw8u`/`raw32u`
before handing bytes to `QImage::loadFromData()` -- the browser is simply doing that same
construction itself instead of C++/Qt doing it.

### 15.4 Not in the §12 M1 minimal mock scope

Like `video`/`adsb`, `image` is out of scope for a minimal §12 M1 mock; a conforming M1 mock
replies `error UNKNOWN_CHANNEL` to an `image` subscribe. `web/mock/server.ts` (beyond M1)
implements it for local web development (a synthetic `bmp` test pattern, deterministic per
`imageIndex` -- see its own header comment for why `bmp` rather than a real `jpeg` fixture).
