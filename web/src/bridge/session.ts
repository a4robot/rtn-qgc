/**
 * Bridge session bootstrap — drives the PROTOCOL.md handshake over a
 * BridgeClient: on every (re)connect, send `hello` (§1.1) followed by the
 * channel subscriptions (§2.2). The bridge processes messages in order, so
 * the subscribe may ride directly behind the hello without awaiting the ack;
 * per §2.4 each (re)subscribe restarts that stream's seq at 1, which the
 * client's own seq-gap tracking already treats as a fresh stream.
 *
 * Multi-vehicle: the session tracks one "active" vehicle at a time for the
 * telemetry channel (video streams are vehicle-agnostic camera feeds and are
 * unaffected by vehicle switches). `setVehicle` on the returned handle moves
 * the active vehicle, unsubscribing the old telemetry stream and subscribing
 * the new one per PROTOCOL.md §2.2/§3.1 (`{ type: "unsubscribe", channel:
 * "telemetry", vehicleId }` then a fresh `subscribe`). On (re)connect the
 * handshake always (re)subscribes whatever vehicle is active at that moment.
 *
 * Seq-gap recovery (§2.4): BridgeClient can't know subscribe payloads (it
 * has no idea about vehicleId/streamId), so it only signals *which* channel
 * gapped via `onResubscribeNeeded`. This module owns the payloads, so it
 * re-sends the appropriate subscribe(s): telemetry → the current active
 * vehicle; video → every stream this session subscribes to (the client
 * can't distinguish which streamId gapped since video seq tracking is keyed
 * by channel only, so all subscribed streams are refreshed).
 */

import type { BridgeClient, ConnectionState } from "./BridgeClient.ts";

export interface BridgeSessionOptions {
  /** Bridge websocket URL. Default: the mock/ghost port on localhost. */
  url?: string;
  /** Vehicle whose telemetry stream to subscribe first. Default 1. */
  initialVehicleId?: number;
  /**
   * Video streams to subscribe to (§9.1), by numeric streamId — see the
   * type-level note on `Subscribe.streamId` in ./types.ts for why this is a
   * number rather than PROTOCOL.md's example string camId. Each is
   * subscribed (channel: "video") right after the telemetry subscribe, on
   * every (re)connect. Omit/empty to skip video entirely.
   */
  videoStreamIds?: number[];
}

/** Handle returned by {@link startBridgeSession} for driving the live session. */
export interface BridgeSessionHandle {
  /**
   * Switch the active vehicle. No-op if `id` is already active. If currently
   * connected, immediately unsubscribes the previous vehicle's telemetry
   * stream and subscribes the new one (fresh seq per §2.4). If not
   * connected, only updates the active id — the next connect handshake
   * subscribes it.
   */
  setVehicle(id: number): void;
  /** Unbind the state handler and disconnect the client. */
  stop(): void;
}

const wsProtocol = typeof window !== 'undefined' && window.location.protocol === "https:" ? "wss:" : "ws:";
const DEFAULT_URL = typeof window !== 'undefined' ? `${wsProtocol}//${window.location.hostname}:8877` : "ws://127.0.0.1:8877";

/**
 * Persistent bridge-address override (Settings → App → Bridge address).
 * Deriving the host from `window.location` works in a browser, but inside
 * the Tauri mobile shell the page origin is `tauri.localhost` — there is
 * no ghost there, and no address bar to type `?bridge=` into. The saved
 * address (e.g. `ws://192.168.1.50:8877` for a ghost on the drone's
 * companion or a laptop) fills that gap; the `?bridge=` query param still
 * wins when present (dev workflow, see App.tsx).
 */
const BRIDGE_URL_STORAGE_KEY = "rtn-gcs.bridge.url";

/** Read the persisted bridge address. Returns null if unset or storage is unavailable. */
export function loadBridgeUrl(): string | null {
  if (typeof window === "undefined" || !window.localStorage) {
    return null;
  }
  try {
    return window.localStorage.getItem(BRIDGE_URL_STORAGE_KEY) || null;
  } catch {
    return null;
  }
}

/** Persist the bridge address; empty/whitespace clears the override. */
export function saveBridgeUrl(url: string): void {
  if (typeof window === "undefined" || !window.localStorage) {
    return;
  }
  try {
    const trimmed = url.trim();
    if (trimmed) {
      window.localStorage.setItem(BRIDGE_URL_STORAGE_KEY, trimmed);
    } else {
      window.localStorage.removeItem(BRIDGE_URL_STORAGE_KEY);
    }
  } catch {
    // private mode / quota exceeded — override just won't persist.
  }
}

/**
 * The address the session will actually dial: explicit override (query
 * param) > saved override > location-derived default. Pure precedence,
 * exported for tests and for showing the effective address in Settings.
 */
export function resolveBridgeUrl(explicit?: string): string {
  return explicit ?? loadBridgeUrl() ?? DEFAULT_URL;
}

/**
 * Pure switch decision, extracted so the branching is unit-testable without
 * a real BridgeClient/WebSocket: given the currently active vehicle, the
 * requested vehicle, and the live connection state, decide whether the
 * active id actually changes and whether an immediate resubscribe is needed
 * (vs. deferring to the next connect handshake).
 */
export function planVehicleSwitch(
  currentVehicleId: number,
  nextVehicleId: number,
  connectionState: ConnectionState,
): { changed: boolean; resubscribeNow: boolean } {
  const changed = nextVehicleId !== currentVehicleId;
  return { changed, resubscribeNow: changed && connectionState === "connected" };
}

/**
 * Connect and keep the session subscribed across reconnects, tracking one
 * active vehicle for the telemetry channel.
 */
export function startBridgeSession(
  client: BridgeClient,
  options: BridgeSessionOptions = {},
): BridgeSessionHandle {
  let activeVehicleId = options.initialVehicleId ?? 1;
  let msgSeq = 0;
  const nextId = (prefix: string) => `${prefix}-${++msgSeq}`;

  function subscribeTelemetry(vehicleId: number): void {
    client.send({
      type: "subscribe",
      id: nextId("sub-telemetry"),
      channel: "telemetry",
      vehicleId,
    });
  }

  function unsubscribeTelemetry(vehicleId: number): void {
    client.send({
      type: "unsubscribe",
      id: nextId("unsub-telemetry"),
      channel: "telemetry",
      vehicleId,
    });
  }

  // Mission is vehicle-scoped like telemetry (§7): same subscribe lifecycle.
  function subscribeMission(vehicleId: number): void {
    client.send({
      type: "subscribe",
      id: nextId("sub-mission"),
      channel: "mission",
      vehicleId,
    });
  }

  function unsubscribeMission(vehicleId: number): void {
    client.send({
      type: "unsubscribe",
      id: nextId("unsub-mission"),
      channel: "mission",
      vehicleId,
    });
  }

  // Image (§15) is vehicle-scoped like telemetry/mission (§2.4's late-binding subscribe applies
  // the same way): same subscribe lifecycle.
  function subscribeImage(vehicleId: number): void {
    client.send({
      type: "subscribe",
      id: nextId("sub-image"),
      channel: "image",
      vehicleId,
    });
  }

  function unsubscribeImage(vehicleId: number): void {
    client.send({
      type: "unsubscribe",
      id: nextId("unsub-image"),
      channel: "image",
      vehicleId,
    });
  }

  function subscribeVideo(streamId: number): void {
    client.send({
      type: "subscribe",
      id: nextId(`sub-video-${streamId}`),
      channel: "video",
      streamId,
    });
  }

  // §16: settings/links aren't a subscribe-stream (no channel/seq envelope,
  // just a request/response like getParam), but per-connection state that's
  // only worth having once hello succeeds — so the fetch lives in the same
  // handshake as the channel subscribes below, not settingsStore/bindBridge.
  function fetchSettings(): void {
    client.send({ type: "getSettings", id: nextId("get-settings") });
  }

  function fetchLinks(): void {
    client.send({ type: "getLinks", id: nextId("get-links") });
  }

  const unbindState = client.onStateChange((state) => {
    if (state !== "connected") {
      return;
    }
    client.send({
      type: "hello",
      id: nextId("hello"),
      token: "dev",
      protocolVersion: "0.1",
    });
    subscribeTelemetry(activeVehicleId);
    subscribeMission(activeVehicleId);
    subscribeImage(activeVehicleId);
    for (const streamId of options.videoStreamIds ?? []) {
      subscribeVideo(streamId);
    }
    fetchSettings();
    fetchLinks();
  });

  // §2.4: on a detected seq gap, re-subscribe the channel that gapped with
  // a fresh id. The client only tells us *which* channel — this is the one
  // place that knows the vehicleId/streamId payloads to re-send.
  const unbindResubscribe = client.onResubscribeNeeded((channel) => {
    if (channel === "telemetry") {
      subscribeTelemetry(activeVehicleId);
      return;
    }
    if (channel === "mission") {
      subscribeMission(activeVehicleId);
      return;
    }
    if (channel === "image") {
      subscribeImage(activeVehicleId);
      return;
    }
    if (channel === "video") {
      for (const streamId of options.videoStreamIds ?? []) {
        subscribeVideo(streamId);
      }
    }
  });

  client.connect(resolveBridgeUrl(options.url));

  return {
    setVehicle(id: number): void {
      const previousVehicleId = activeVehicleId;
      const { changed, resubscribeNow } = planVehicleSwitch(
        previousVehicleId,
        id,
        client.connectionState,
      );
      if (!changed) {
        return;
      }
      activeVehicleId = id;
      if (resubscribeNow) {
        unsubscribeTelemetry(previousVehicleId);
        unsubscribeMission(previousVehicleId);
        unsubscribeImage(previousVehicleId);
        subscribeTelemetry(id);
        subscribeMission(id);
        subscribeImage(id);
      }
      // else: not connected — the next connect handshake subscribes `id`.
    },
    stop(): void {
      unbindState();
      unbindResubscribe();
      client.disconnect();
    },
  };
}
