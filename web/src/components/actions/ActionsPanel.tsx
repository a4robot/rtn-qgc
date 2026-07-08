/**
 * Actions panel — guided-action command buttons (arm/disarm/takeoff/land/
 * rtl/pause) with ack/progress feedback, PROTOCOL.md §5.
 *
 * Each button sends a `command` through the bridge client and tracks its
 * lifecycle (pending -> accepted|rejected, plus streamed progress) via
 * `useCommandRequests`. Destructive actions (arm, disarm-while-armed,
 * takeoff, land, rtl) require a slide-to-confirm gesture (`Slider`, see
 * ../guided/Slider.tsx) instead of a plain click; PAUSE is non-destructive
 * and stays a plain button.
 */

import { useCallback, useEffect, useState } from "react";

import type { BridgeClient } from "../../bridge/BridgeClient.ts";
import type { CommandAction } from "../../bridge/types.ts";
import { useConnection, useVehicle } from "../../store/index.ts";
import { Slider } from "../guided/Slider.tsx";
import { latestRequestForAction, useCommandRequests, type CommandRequestState } from "./useCommandRequests.ts";
import "./actions.css";

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
  const [notConnectedNote, setNotConnectedNote] = useState(false);

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
            danger
            disabled={!ready}
            request={latestRequestForAction(requests, "disarm")}
            onClick={() => dispatchCommand("disarm")}
          />
        ) : (
          <ActionButton
            label="ARM"
            tone="critical"
            danger
            disabled={!ready}
            request={latestRequestForAction(requests, "arm")}
            onClick={() => dispatchCommand("arm")}
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
            request={latestRequestForAction(requests, "takeoff")}
            onClick={() => dispatchCommand("takeoff", { alt: altitude })}
          />
        </div>

        <ActionButton
          label="LAND"
          tone="critical"
          danger
          disabled={!ready || !armed}
          request={latestRequestForAction(requests, "land")}
          onClick={() => dispatchCommand("land")}
        />

        <ActionButton
          label="RTL"
          tone="critical"
          danger
          disabled={!ready || !armed}
          request={latestRequestForAction(requests, "rtl")}
          onClick={() => dispatchCommand("rtl")}
        />

        <ActionButton
          label="PAUSE"
          plain
          disabled={!ready || !armed}
          request={latestRequestForAction(requests, "pause")}
          onClick={() => dispatchCommand("pause")}
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
  /** Tints the slider red and requires the full slide gesture. */
  danger?: boolean;
  /** Non-destructive actions (PAUSE) keep the plain click button. */
  plain?: boolean;
  request?: CommandRequestState;
}

function ActionButton({ label, onClick, disabled, tone, danger, plain, request }: ActionButtonProps) {
  const pending = request?.phase === "pending";
  const phaseClass =
    request?.phase === "accepted"
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
      {plain ? (
        <button
          type="button"
          className={`actions-button ${phaseClass}`.trim()}
          disabled={disabled || pending}
          onClick={onClick}
        >
          {pending && <span className="actions-spinner" aria-hidden="true" />}
          {label}
        </button>
      ) : (
        <div className="actions-slider-slot">
          <Slider
            label={label}
            onConfirm={onClick}
            disabled={disabled || pending}
            danger={danger}
            className={
              request?.phase === "accepted"
                ? "actions-slider--accepted"
                : request?.phase === "rejected"
                  ? "actions-slider--rejected"
                  : undefined
            }
          />
          {pending && <span className="actions-spinner actions-spinner--overlay" aria-hidden="true" />}
        </div>
      )}
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
