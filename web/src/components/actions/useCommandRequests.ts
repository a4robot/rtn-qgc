/**
 * Request-tracking reducer + hook for the Actions panel command lifecycle.
 *
 * PROTOCOL.md §5: one `commandAck` per request id (accepted|rejected), plus
 * zero or more unsolicited `commandProgress` messages. This module tracks
 * in-flight requests keyed by id so the UI can render pending/accepted/
 * rejected state and a progress readout per button, without a global store.
 *
 * The reducer (`commandRequestsReducer`) is pure and exported standalone for
 * testing; `useCommandRequests` wires it to a BridgeClient's command
 * responses and owns the send + auto-clear side effects.
 */

import { useCallback, useEffect, useReducer, useRef } from "react";

import type { BridgeClient } from "../../bridge/BridgeClient.ts";
import type { CommandAction } from "../../bridge/types.ts";

export type RequestPhase = "pending" | "accepted" | "rejected";

export interface CommandRequestState {
  action: CommandAction;
  vehicleId: number;
  phase: RequestPhase;
  /** Rejection cause, present once phase is "rejected" and reason was given. */
  reason?: string;
  /** Latest commandProgress value, 0..1 or null (indeterminate) once streamed. */
  progress?: number | null;
  message?: string;
  sentAtMs: number;
}

export type CommandRequestsState = Record<string, CommandRequestState>;

export type CommandRequestsAction =
  | { type: "sent"; id: string; action: CommandAction; vehicleId: number; atMs: number }
  | { type: "ack"; id: string; status: "accepted" | "rejected"; reason?: string }
  | { type: "progress"; id: string; progress: number | null; message?: string }
  | { type: "clear"; id: string };

/** Milliseconds an accepted request's success flash stays visible before clearing. */
export const ACCEPTED_CLEAR_MS = 1500;
/** Milliseconds a rejected request's reason stays visible before clearing. */
export const REJECTED_CLEAR_MS = 6000;

export function commandRequestsReducer(
  state: CommandRequestsState,
  action: CommandRequestsAction,
): CommandRequestsState {
  switch (action.type) {
    case "sent": {
      return {
        ...state,
        [action.id]: {
          action: action.action,
          vehicleId: action.vehicleId,
          phase: "pending",
          sentAtMs: action.atMs,
        },
      };
    }
    case "ack": {
      const existing = state[action.id];
      if (!existing) {
        return state; // response for a request we aren't tracking (or already cleared)
      }
      return {
        ...state,
        [action.id]: { ...existing, phase: action.status, reason: action.reason },
      };
    }
    case "progress": {
      const existing = state[action.id];
      if (!existing) {
        return state;
      }
      return {
        ...state,
        [action.id]: { ...existing, progress: action.progress, message: action.message },
      };
    }
    case "clear": {
      if (!(action.id in state)) {
        return state;
      }
      const next = { ...state };
      delete next[action.id];
      return next;
    }
    default:
      return state;
  }
}

export interface UseCommandRequestsResult {
  requests: CommandRequestsState;
  /**
   * Send a command through the bridge client and start tracking it.
   * Returns the generated request id and whether the send succeeded
   * (false means the socket wasn't open — caller should surface a note).
   */
  send: (
    vehicleId: number,
    action: CommandAction,
    params: Record<string, string | number | boolean>,
  ) => { id: string; ok: boolean };
}

/** Wires `commandRequestsReducer` to a BridgeClient's command response stream. */
export function useCommandRequests(client: BridgeClient): UseCommandRequestsResult {
  const [requests, dispatch] = useReducer(commandRequestsReducer, {});
  const timersRef = useRef(new Map<string, ReturnType<typeof setTimeout>>());
  const counterRef = useRef(0);

  const scheduleClear = useCallback((id: string, delayMs: number) => {
    const existing = timersRef.current.get(id);
    if (existing !== undefined) {
      clearTimeout(existing);
    }
    const timer = setTimeout(() => {
      timersRef.current.delete(id);
      dispatch({ type: "clear", id });
    }, delayMs);
    timersRef.current.set(id, timer);
  }, []);

  useEffect(() => {
    return client.onCommandResponse((message) => {
      if (message.type === "commandAck") {
        dispatch({ type: "ack", id: message.id, status: message.status, reason: message.reason });
        scheduleClear(message.id, message.status === "rejected" ? REJECTED_CLEAR_MS : ACCEPTED_CLEAR_MS);
      } else {
        dispatch({ type: "progress", id: message.id, progress: message.progress, message: message.message });
      }
    });
  }, [client, scheduleClear]);

  // Timers are per-mount; clear them all on unmount to avoid dispatching
  // into an unmounted hook's stale closures (React ignores it, but tidy).
  useEffect(() => {
    const timers = timersRef.current;
    return () => {
      for (const timer of timers.values()) {
        clearTimeout(timer);
      }
      timers.clear();
    };
  }, []);

  const send = useCallback(
    (vehicleId: number, action: CommandAction, params: Record<string, string | number | boolean>) => {
      const id = `cmd-${counterRef.current++}-${action}`;
      const ok = client.send({ type: "command", id, vehicleId, action, params });
      if (ok) {
        dispatch({ type: "sent", id, action, vehicleId, atMs: Date.now() });
      }
      return { id, ok };
    },
    [client],
  );

  return { requests, send };
}

/** Most recent tracked request for a given action, if any (for per-button rendering). */
export function latestRequestForAction(
  requests: CommandRequestsState,
  action: CommandAction,
): CommandRequestState | undefined {
  let latest: CommandRequestState | undefined;
  for (const request of Object.values(requests)) {
    if (request.action !== action) {
      continue;
    }
    if (!latest || request.sentAtMs >= latest.sentAtMs) {
      latest = request;
    }
  }
  return latest;
}
