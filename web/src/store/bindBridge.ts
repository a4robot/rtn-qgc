/**
 * bindBridge — glue between the framework-agnostic BridgeClient and the
 * zustand stores. The only place bridge messages are dispatched into state.
 *
 * Channel → store mapping:
 *  - connection state changes  → connectionStore.setState
 *  - "tick"                    → connectionStore.applyTick + vehicleStore.syncVehicleIds
 *  - "telemetry"               → vehicleStore.applyTelemetry
 *  - "mission"                  → missionStore.applyMissionState
 *  - "image"                    → imageStore.applyImage
 *  - notification               → notificationStore.addNotification
 *  - settingsValue              → settingsStore.applySettings (upsert — the getSettings snapshot
 *                                  session.ts fires on connect needs somewhere to land even before
 *                                  the SETTINGS tab is ever opened, same reason paramValue is wired
 *                                  here; a setSetting's one-entry response harmlessly re-applies
 *                                  too, since upsert is idempotent)
 *  - settingChanged             → settingsStore.applySettingChanged (§16.5's authoritative push)
 *  - linksValue                 → settingsStore.applyLinksSnapshot (same "needs a home before the
 *                                  tab opens" reasoning as settingsValue above)
 *  - linkAck NOT handled here — deliberately: it's a request/response for one specific
 *    add/remove/connect/disconnect (mirrors missionAck/missionItems, which are likewise NOT wired
 *    here — see WaypointList.tsx's own `useMissionRequest`); SettingsPanel.tsx's `useLinkRequests`
 *    owns that lifecycle (pending flags, errors, triggering a follow-up getLinks) directly.
 *  - seq gaps                  → connectionStore.recordSeqGap
 */

import type { BridgeClient } from "../bridge/BridgeClient.ts";
import { useConnectionStore } from "./connectionStore.ts";
import { useVehicleStore } from "./vehicleStore.ts";
import { useParamStore } from "./paramStore.ts";
import { useMissionStore } from "./missionStore.ts";
import { useImageStore } from "./imageStore.ts";
import { useNotificationStore } from "./notificationStore.ts";
import { useSettingsStore } from "./settingsStore.ts";

/**
 * Subscribe the stores to a BridgeClient. Returns a cleanup function that
 * removes every subscription (the client itself is left untouched).
 */
export function bindBridgeToStores(client: BridgeClient): () => void {
  const connection = useConnectionStore.getState();
  const vehicles = useVehicleStore.getState();
  const params = useParamStore.getState();
  const missions = useMissionStore.getState();
  const images = useImageStore.getState();
  const notifications = useNotificationStore.getState();
  const settings = useSettingsStore.getState();

  // Reflect the client's current state immediately; onStateChange only
  // fires on transitions.
  connection.setState(client.connectionState);

  const unsubscribers = [
    client.onStateChange((state) => {
      useConnectionStore.getState().setState(state);
    }),

    client.onSeqGap((gap) => {
      useConnectionStore.getState().recordSeqGap(gap);
    }),

    client.subscribe("tick", (message) => {
      if (!("type" in message) || message.type !== "tick") {
        return;
      }
      connection.applyTick(message);
      vehicles.syncVehicleIds(message.vehicleIds);
    }),

    client.subscribe("telemetry", (message) => {
      if (!("channel" in message) || message.channel !== "telemetry") {
        return;
      }
      vehicles.applyTelemetry(message);
    }),

    client.subscribe("mission", (message) => {
      if (!("channel" in message) || message.channel !== "mission") {
        return;
      }
      // subscribeAck also rides the mission channel — only missionState
      // snapshots (they alone carry `items`) belong in the store.
      if (!("items" in message) || !Array.isArray(message.items)) {
        return;
      }
      missions.applyMissionState(message);
    }),

    client.subscribe("image", (message) => {
      if (!("channel" in message) || message.channel !== "image") {
        return;
      }
      // Only messages carrying `data` (the payload every real §15 `image`
      // message has) belong in the store — mirrors the mission subscriber's
      // `items` guard above.
      if (!("data" in message) || typeof message.data !== "string") {
        return;
      }
      images.applyImage(message);
    }),

    client.onParamValue((message) => {
      params.applyParamValue(message);
    }),

    client.onNotification((message) => {
      notifications.addNotification(message);
    }),

    // §16.3/§16.4: settingsValue answers both getSettings and setSetting (one shared shape) — see
    // this module's header comment for why it's wired here rather than left to SettingsPanel alone.
    client.onSettingsResponse((message) => {
      if (message.type === "settingsValue") {
        settings.applySettings(message.settings);
      }
    }),

    // §16.5: the authoritative "a whitelisted setting's value changed" broadcast.
    client.onSettingChanged((message) => {
      settings.applySettingChanged(message.group, message.name, message.value, message.meta);
    }),

    // §16.7: only the getLinks snapshot lands here — linkAck is component-owned, see header comment.
    client.onLinksResponse((message) => {
      if (message.type === "linksValue") {
        settings.applyLinksSnapshot(message.links);
      }
    }),
  ];

  return () => {
    for (const unsubscribe of unsubscribers) {
      unsubscribe();
    }
  };
}
