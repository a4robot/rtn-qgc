/**
 * Connection store — bridge link health as renderable state.
 *
 * Tracks the WebSocket connection state, reconnect attempts, and heartbeat
 * (`tick`) derived values: last tick time, server clock offset (smoothed over
 * several ticks per PROTOCOL.md §11.1), and the set of connected vehicles.
 */

import { create } from "zustand";

import type { ConnectionState, SeqGap } from "../bridge/BridgeClient.ts";
import type { Tick } from "../bridge/types.ts";

/**
 * Protocol §11.1 tick fields not yet present in bridge/types.ts (additive,
 * so optional per the versioning rules). Extended here rather than editing
 * the shared contract types.
 */
export type TickMessage = Tick & {
  /** Server timestamp, microseconds since epoch. */
  serverTimeUs?: number;
  /** Bridge uptime, seconds. */
  uptimeS?: number;
};

/** Exponential-moving-average weight for clock-offset smoothing. */
const CLOCK_OFFSET_SMOOTHING = 0.2;

export interface ConnectionStoreState {
  /** Current WebSocket connection state (mirrors BridgeClient). */
  state: ConnectionState;
  /** Reconnect attempts since the link was last "connected". */
  reconnectAttempts: number;
  /** Local receipt time of the last tick, ms since epoch. Null before first tick. */
  lastTickAtMs: number | null;
  /** Server time carried by the last tick, microseconds. Null before first tick. */
  lastTickServerTimeUs: number | null;
  /** Smoothed (serverTime - localTime) estimate, ms. Null before first tick. */
  clockOffsetMs: number | null;
  /** Vehicle ids the bridge currently knows about (from the last tick). */
  vehicleIds: number[];
  /** Last detected per-channel seq gap, for surfacing/diagnostics. */
  lastSeqGap: SeqGap | null;

  setState: (state: ConnectionState) => void;
  applyTick: (tick: TickMessage, receivedAtMs?: number) => void;
  recordSeqGap: (gap: SeqGap) => void;
}

export const useConnectionStore = create<ConnectionStoreState>()((set) => ({
  state: "disconnected",
  reconnectAttempts: 0,
  lastTickAtMs: null,
  lastTickServerTimeUs: null,
  clockOffsetMs: null,
  vehicleIds: [],
  lastSeqGap: null,

  setState: (state) =>
    set((prev) => ({
      state,
      reconnectAttempts:
        state === "connected"
          ? 0
          : state === "reconnecting"
            ? prev.reconnectAttempts + 1
            : prev.reconnectAttempts,
    })),

  applyTick: (tick, receivedAtMs = Date.now()) =>
    set((prev) => {
      const serverTimeUs = tick.serverTimeUs ?? tick.timestampMs * 1000;
      const rawOffsetMs = serverTimeUs / 1000 - receivedAtMs;
      const clockOffsetMs =
        prev.clockOffsetMs === null
          ? rawOffsetMs
          : prev.clockOffsetMs +
            CLOCK_OFFSET_SMOOTHING * (rawOffsetMs - prev.clockOffsetMs);
      return {
        lastTickAtMs: receivedAtMs,
        lastTickServerTimeUs: serverTimeUs,
        clockOffsetMs,
        vehicleIds: tick.vehicleIds,
      };
    }),

  recordSeqGap: (gap) => set({ lastSeqGap: gap }),
}));

/** Connection state for components. Optionally pass a selector to limit re-renders. */
export function useConnection(): ConnectionStoreState;
export function useConnection<T>(selector: (state: ConnectionStoreState) => T): T;
export function useConnection<T>(
  selector?: (state: ConnectionStoreState) => T,
): ConnectionStoreState | T {
  return selector === undefined ? useConnectionStore() : useConnectionStore(selector);
}
