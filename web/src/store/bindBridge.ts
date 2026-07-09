/**
 * bindBridge — glue between the framework-agnostic BridgeClient and the
 * zustand stores. The only place bridge messages are dispatched into state.
 *
 * Channel → store mapping:
 *  - connection state changes  → connectionStore.setState
 *  - "tick"                    → connectionStore.applyTick + vehicleStore.syncVehicleIds
 *  - "telemetry"               → vehicleStore.applyTelemetry
 *  - "mission"                  → missionStore.applyMissionState
 *  - notification               → notificationStore.addNotification
 *  - seq gaps                  → connectionStore.recordSeqGap
 */

import type { BridgeClient } from "../bridge/BridgeClient.ts";
import { useConnectionStore } from "./connectionStore.ts";
import { useVehicleStore } from "./vehicleStore.ts";
import { useParamStore } from "./paramStore.ts";
import { useMissionStore } from "./missionStore.ts";
import { useNotificationStore } from "./notificationStore.ts";

/**
 * Subscribe the stores to a BridgeClient. Returns a cleanup function that
 * removes every subscription (the client itself is left untouched).
 */
export function bindBridgeToStores(client: BridgeClient): () => void {
  const connection = useConnectionStore.getState();
  const vehicles = useVehicleStore.getState();
  const params = useParamStore.getState();
  const missions = useMissionStore.getState();
  const notifications = useNotificationStore.getState();

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

    client.onParamValue((message) => {
      params.applyParamValue(message);
    }),

    client.onNotification((message) => {
      notifications.addNotification(message);
    }),
  ];

  return () => {
    for (const unsubscribe of unsubscribers) {
      unsubscribe();
    }
  };
}
