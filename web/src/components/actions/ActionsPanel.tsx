/**
 * Actions panel — guided-action command buttons (arm/disarm/takeoff/land/
 * rtl/pause) with ack/progress feedback, PROTOCOL.md §5.
 *
 * Each button sends a `command` through the bridge client and tracks its
 * lifecycle (pending -> accepted|rejected, plus streamed progress) via
 * `useCommandRequests`. Destructive actions (arm, takeoff, land, rtl)
 * require a two-step press-then-confirm within a 3s window.
 */

import { useCallback, useEffect, useState } from "react";

import type { BridgeClient } from "../../bridge/BridgeClient.ts";
import type { CommandAction } from "../../bridge/types.ts";
import { useConnection, useVehicle } from "../../store/index.ts";
import { latestRequestForAction, useCommandRequests, type CommandRequestState } from "./useCommandRequests.ts";
import "./actions.css";

/** How long a press-to-confirm stays armed before it must be re-pressed. */
const CONFIRM_WINDOW_MS = 3000;
/** How long the "not connected" note stays visible after a dropped send. */
const NOT_CONNECTED_NOTE_MS = 2500;

const DEFAULT_TAKEOFF_ALT_M = 20;

export interface ActionsPanelProps {
  client: BridgeClient;
  vehicleId: number;
}

export function ActionsPanel({ client, vehicleId }: ActionsPanelProps) {
  const vehicle = useVehicle(vehicleId);
  const connectionState = useConnection((state) => state.state);
  const { requests, send } = useCommandRequests(client);

  const [altitude, setAltitude] = useState(DEFAULT_TAKEOFF_ALT_M);
  const [confirming, setConfirming] = useState<{ action: CommandAction; expiresAtMs: number } | null>(null);
  const [notConnectedNote, setNotConnectedNote] = useState(false);

  // Auto-drop the confirm state once its window elapses.
  useEffect(() => {
    if (!confirming) {
      return;
    }
    const remainingMs = Math.max(confirming.expiresAtMs - Date.now(), 0);
    const timer = setTimeout(() => setConfirming(null), remainingMs);
    return () => clearTimeout(timer);
  }, [confirming]);

  // Auto-clear the "not connected" note.
  useEffect(() => {
    if (!notConnectedNote) {
      return;
    }
    const timer = setTimeout(() => setNotConnectedNote(false), NOT_CONNECTED_NOTE_MS);
    return () => clearTimeout(timer);
  }, [notConnectedNote]);

  const ready = Boolean(vehicle) && connectionState === "connected";

  const dispatchCommand = useCallback(
    (action: CommandAction, params: Record<string, string | number | boolean> = {}) => {
      const { ok } = send(vehicleId, action, params);
      if (!ok) {
        setNotConnectedNote(true);
      }
    },
    [send, vehicleId],
  );

  const handlePress = useCallback(
    (action: CommandAction, destructive: boolean, params?: Record<string, string | number | boolean>) => {
      if (!destructive) {
        dispatchCommand(action, params);
        return;
      }
      const isConfirmed = confirming?.action === action && confirming.expiresAtMs > Date.now();
      if (isConfirmed) {
        setConfirming(null);
        dispatchCommand(action, params);
      } else {
        setConfirming({ action, expiresAtMs: Date.now() + CONFIRM_WINDOW_MS });
      }
    },
    [confirming, dispatchCommand],
  );

  if (!vehicle) {
    return (
      <div className="actions-panel actions-panel--empty" aria-label="Actions">
        <span className="actions-placeholder">No vehicle data</span>
      </div>
    );
  }

  const armed = vehicle.armed;

  return (
    <div className="actions-panel" aria-label="Actions">
      {notConnectedNote && (
        <div className="actions-note" role="status">
          Not connected — command not sent
        </div>
      )}
      <div className="actions-grid">
        {armed ? (
          <ActionButton
            label="DISARM"
            disabled={!ready}
            request={latestRequestForAction(requests, "disarm")}
            onClick={() => handlePress("disarm", false)}
          />
        ) : (
          <ActionButton
            label="ARM"
            tone="critical"
            disabled={!ready}
            confirming={confirming?.action === "arm"}
            request={latestRequestForAction(requests, "arm")}
            onClick={() => handlePress("arm", true)}
          />
        )}

        <div className="actions-tile actions-tile--wide">
          <div className="actions-altitude-row">
            <label htmlFor="actions-takeoff-alt">Takeoff alt (m)</label>
            <input
              id="actions-takeoff-alt"
              className="actions-altitude-input"
              type="number"
              min={1}
              step={1}
              value={altitude}
              disabled={!ready}
              onChange={(event) => setAltitude(Number(event.target.value))}
            />
          </div>
          <ActionButton
            label="TAKEOFF"
            tone="accent"
            disabled={!ready || !armed}
            confirming={confirming?.action === "takeoff"}
            request={latestRequestForAction(requests, "takeoff")}
            onClick={() => handlePress("takeoff", true, { alt: altitude })}
          />
        </div>

        <ActionButton
          label="LAND"
          tone="critical"
          disabled={!ready || !armed}
          confirming={confirming?.action === "land"}
          request={latestRequestForAction(requests, "land")}
          onClick={() => handlePress("land", true)}
        />

        <ActionButton
          label="RTL"
          tone="critical"
          disabled={!ready || !armed}
          confirming={confirming?.action === "rtl"}
          request={latestRequestForAction(requests, "rtl")}
          onClick={() => handlePress("rtl", true)}
        />

        <ActionButton
          label="PAUSE"
          disabled={!ready || !armed}
          request={latestRequestForAction(requests, "pause")}
          onClick={() => handlePress("pause", false)}
        />
      </div>
    </div>
  );
}

interface ActionButtonProps {
  label: string;
  onClick: () => void;
  disabled?: boolean;
  tone?: "accent" | "critical";
  confirming?: boolean;
  request?: CommandRequestState;
}

function ActionButton({ label, onClick, disabled, tone, confirming, request }: ActionButtonProps) {
  const pending = request?.phase === "pending";
  const phaseClass = confirming
    ? "actions-button--confirm"
    : request?.phase === "accepted"
      ? "actions-button--accepted"
      : request?.phase === "rejected"
        ? "actions-button--rejected"
        : pending
          ? "actions-button--pending"
          : tone
            ? `actions-button--${tone}`
            : "";

  return (
    <div className="actions-tile">
      <button
        type="button"
        className={`actions-button ${phaseClass}`.trim()}
        disabled={disabled || pending}
        onClick={onClick}
      >
        {pending && <span className="actions-spinner" aria-hidden="true" />}
        {confirming ? `Confirm ${label}?` : label}
      </button>
      {request?.phase === "rejected" && request.reason && (
        <span className="actions-reason" role="alert">
          {request.reason}
        </span>
      )}
      {request && request.progress !== undefined && request.phase !== "rejected" && (
        <div className="actions-progress">
          <div className="actions-progress-bar">
            <div
              className={`actions-progress-fill${request.progress === null ? " actions-progress-fill--indeterminate" : ""}`}
              style={request.progress === null ? undefined : { width: `${Math.round(request.progress * 100)}%` }}
            />
          </div>
          <span className="actions-progress-label">
            {request.progress === null ? "…" : `${Math.round(request.progress * 100)}%`}
            {request.message ? ` ${request.message}` : ""}
          </span>
        </div>
      )}
    </div>
  );
}
