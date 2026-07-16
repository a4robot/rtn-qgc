/**
 * Actions panel — guided-action command buttons (arm/disarm/takeoff/land/
 * rtl/pause) with ack/progress feedback, PROTOCOL.md §5.
 *
 * Interaction model (QGC-style, two steps): every action is a plain
 * button; pressing a destructive one (arm, disarm-while-armed, takeoff,
 * land, rtl) arms a SINGLE slide-to-confirm row for that action instead
 * of dispatching. Sliding confirms and sends; ✕ / Escape / pressing
 * another action / a timeout cancels. One slider at a time keeps the
 * strip compact — a stack of always-visible sliders read as clutter.
 * PAUSE is non-destructive and dispatches on click.
 *
 * Each dispatch tracks its lifecycle (pending -> accepted|rejected, plus
 * streamed progress) via `useCommandRequests`.
 */

import { useCallback, useEffect, useState } from "react";

import type { BridgeClient } from "../../bridge/BridgeClient.ts";
import type { CommandAction } from "../../bridge/types.ts";
import { useConnection, useVehicle } from "../../store/index.ts";
import { Slider } from "../guided/Slider.tsx";
import { latestRequestForAction, useCommandRequests, type CommandRequestState } from "./useCommandRequests.ts";
import { useTranslation } from "react-i18next";
import "./actions.css";

/** How long the "not connected" note stays visible after a dropped send. */
const NOT_CONNECTED_NOTE_MS = 2500;

/** An armed-but-unconfirmed action auto-cancels after this long. */
const CONFIRM_TIMEOUT_MS = 8000;

const DEFAULT_TAKEOFF_ALT_M = 20;

export interface ActionsPanelProps {
  client: BridgeClient;
  vehicleId: number;
}

export function ActionsPanel({ client, vehicleId }: ActionsPanelProps) {
  const { t } = useTranslation();
  const vehicle = useVehicle(vehicleId);
  const connectionState = useConnection((state) => state.state);
  const { requests, send } = useCommandRequests(client);

  const [altitude, setAltitude] = useState(DEFAULT_TAKEOFF_ALT_M);
  const [notConnectedNote, setNotConnectedNote] = useState(false);
  /** The destructive action awaiting its slide confirmation, if any. */
  const [confirming, setConfirming] = useState<CommandAction | null>(null);

  // Auto-clear the "not connected" note.
  useEffect(() => {
    if (!notConnectedNote) {
      return;
    }
    const timer = setTimeout(() => setNotConnectedNote(false), NOT_CONNECTED_NOTE_MS);
    return () => clearTimeout(timer);
  }, [notConnectedNote]);

  // Auto-cancel a stale confirm row; Escape cancels immediately.
  useEffect(() => {
    if (!confirming) {
      return;
    }
    const timer = setTimeout(() => setConfirming(null), CONFIRM_TIMEOUT_MS);
    const onKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        setConfirming(null);
      }
    };
    window.addEventListener("keydown", onKey);
    return () => {
      clearTimeout(timer);
      window.removeEventListener("keydown", onKey);
    };
  }, [confirming]);

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

  /** Confirmed destructive dispatch — params resolved at confirm time. */
  const confirmPending = useCallback(() => {
    if (!confirming) {
      return;
    }
    dispatchCommand(confirming, confirming === "takeoff" ? { alt: altitude } : {});
    setConfirming(null);
  }, [confirming, dispatchCommand, altitude]);

  if (!vehicle) {
    return (
      <div className="actions-panel actions-panel--empty" aria-label="Actions">
        <span className="actions-placeholder">{t("No vehicle data")}</span>
      </div>
    );
  }

  const armed = vehicle.armed;

  return (
    <div className="actions-panel" aria-label="Actions">
      {notConnectedNote && (
        <div className="actions-note" role="status">
          {t("Not connected — command not sent")}
        </div>
      )}
      <div className={`actions-grid${confirming ? " actions-grid--confirming" : ""}`}>
        {armed ? (
          <ActionButton
            label={t("DISARM")}
            tone="critical"
            disabled={!ready}
            active={confirming === "disarm"}
            request={latestRequestForAction(requests, "disarm")}
            onClick={() => setConfirming("disarm")}
          />
        ) : (
          <ActionButton
            label={t("ARM")}
            tone="critical"
            disabled={!ready}
            active={confirming === "arm"}
            request={latestRequestForAction(requests, "arm")}
            onClick={() => setConfirming("arm")}
          />
        )}

        <div className="actions-tile actions-tile--wide">
          <div className="actions-altitude-row">
            <label htmlFor="actions-takeoff-alt">{t("Takeoff alt (m)")}</label>
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
            label={t("TAKEOFF")}
            tone="accent"
            disabled={!ready || !armed}
            active={confirming === "takeoff"}
            request={latestRequestForAction(requests, "takeoff")}
            onClick={() => setConfirming("takeoff")}
          />
        </div>

        <ActionButton
          label={t("LAND")}
          tone="critical"
          disabled={!ready || !armed}
          active={confirming === "land"}
          request={latestRequestForAction(requests, "land")}
          onClick={() => setConfirming("land")}
        />

        <ActionButton
          label={t("RTL")}
          tone="critical"
          disabled={!ready || !armed}
          active={confirming === "rtl"}
          request={latestRequestForAction(requests, "rtl")}
          onClick={() => setConfirming("rtl")}
        />

        <ActionButton
          label={t("PAUSE")}
          plain
          disabled={!ready || !armed}
          request={latestRequestForAction(requests, "pause")}
          onClick={() => dispatchCommand("pause")}
        />
      </div>

      {confirming && (
        <div className="actions-confirm-row" role="group" aria-label="Confirm action">
          <div className="actions-confirm-slider">
            <Slider
              label={`${t(confirming.toUpperCase())}${confirming === "takeoff" ? ` · ${altitude}m` : ""}`}
              onConfirm={confirmPending}
              danger={confirming !== "takeoff"}
            />
          </div>
          <button
            type="button"
            className="actions-confirm-cancel"
            aria-label="Cancel"
            onClick={() => setConfirming(null)}
          >
            ✕
          </button>
        </div>
      )}
    </div>
  );
}

interface ActionButtonProps {
  label: string;
  onClick: () => void;
  disabled?: boolean;
  tone?: "accent" | "critical";
  /** Non-destructive actions (PAUSE) dispatch straight from the click. */
  plain?: boolean;
  /** True while this action's confirm slider is armed below the grid. */
  active?: boolean;
  request?: CommandRequestState;
}

function ActionButton({ label, onClick, disabled, tone, plain, active, request }: ActionButtonProps) {
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
      <button
        type="button"
        className={`actions-button ${phaseClass}${active ? " actions-button--confirming" : ""}`.trim()}
        disabled={disabled || pending}
        onClick={onClick}
      >
        {active && (
          <svg className="actions-button-ring" xmlns="http://www.w3.org/2000/svg">
            <rect width="100%" height="100%" rx="17" ry="17" pathLength="100" />
          </svg>
        )}
        {pending && <span className="actions-spinner" aria-hidden="true" />}
        {label}
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
