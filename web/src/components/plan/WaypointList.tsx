/**
 * Plan-mode panel, PROTOCOL.md §7 — the draft mission editor: per-item
 * seq/coords/altitude with remove, "Load from vehicle" (seeds the draft from
 * the on-vehicle mission already mirrored in missionStore — no bridge
 * round-trip), "Upload" (`missionUpload`, §7.2) and "Clear vehicle"
 * (`missionClear`, §7.2), both tracked via `client.onMissionResponse` and
 * keyed by request id, mirroring `useCommandRequests`'s ack-tracking pattern
 * for the command channel (§5).
 *
 * Only one mission request (upload or clear) is tracked at a time — both
 * buttons are disabled while either is in flight, since overlapping
 * upload/clear calls against the same vehicle mission would race.
 *
 * The FLY/PLAN map-click-mode toggle that used to live here now lives in the
 * top toolbar (`TopToolbar`'s view switcher) — this component only mounts
 * while that toolbar already has "plan" selected (see `PlanDrawer.tsx`), so
 * a second in-panel toggle would be redundant. `uiStore.mapMode` is still
 * the thing that actually switches `GotoOnClick`/`WaypointAdder` on the map;
 * this component just no longer has its own control for it.
 */

import { useCallback, useEffect, useRef, useState } from "react";

import type { BridgeClient } from "../../bridge/BridgeClient.ts";
import type { MissionItem } from "../../bridge/types.ts";
import { useMission, usePlanItems, usePlanStore } from "../../store/index.ts";
import { useTranslation } from "react-i18next";
import "./WaypointList.css";

/** How long an accepted upload/clear's confirmation stays visible before clearing. */
const ACCEPTED_CLEAR_MS = 2000;
/** How long a rejected upload/clear's reason stays visible before clearing. */
const REJECTED_CLEAR_MS = 6000;

interface MissionRequestState {
  kind: "upload" | "clear";
  phase: "pending" | "accepted" | "rejected";
  itemCount?: number;
  reason?: string;
}

/**
 * Tracks a single in-flight missionUpload/missionClear request against
 * `client.onMissionResponse`, keyed by request id — same request/response
 * shape as command acks (§5.2) applied to §7.2's missionAck.
 */
function useMissionRequest(client: BridgeClient) {
  const [request, setRequest] = useState<MissionRequestState | null>(null);
  const pendingIdRef = useRef<string | null>(null);
  const counterRef = useRef(0);
  const clearTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    return client.onMissionResponse((message) => {
      if (message.type !== "missionAck" || message.id !== pendingIdRef.current) {
        return; // missionItems (unused here) or a response for a request we dropped
      }
      setRequest((prev) =>
        prev
          ? { ...prev, phase: message.status, reason: message.reason, itemCount: message.itemCount }
          : prev,
      );
      if (clearTimerRef.current !== null) {
        clearTimeout(clearTimerRef.current);
      }
      clearTimerRef.current = setTimeout(
        () => {
          pendingIdRef.current = null;
          setRequest(null);
        },
        message.status === "rejected" ? REJECTED_CLEAR_MS : ACCEPTED_CLEAR_MS,
      );
    });
  }, [client]);

  useEffect(() => {
    return () => {
      if (clearTimerRef.current !== null) {
        clearTimeout(clearTimerRef.current);
      }
    };
  }, []);

  const sendUpload = useCallback(
    (vehicleId: number, items: MissionItem[]) => {
      const id = `plan-upload-${counterRef.current++}`;
      const ok = client.send({ type: "missionUpload", id, vehicleId, items });
      if (ok) {
        pendingIdRef.current = id;
        setRequest({ kind: "upload", phase: "pending" });
      }
      return ok;
    },
    [client],
  );

  const sendClear = useCallback(
    (vehicleId: number) => {
      const id = `plan-clear-${counterRef.current++}`;
      const ok = client.send({ type: "missionClear", id, vehicleId });
      if (ok) {
        pendingIdRef.current = id;
        setRequest({ kind: "clear", phase: "pending" });
      }
      return ok;
    },
    [client],
  );

  return { request, sendUpload, sendClear };
}

export interface WaypointListProps {
  client: BridgeClient;
  vehicleId: number;
}

export function WaypointList({ client, vehicleId }: WaypointListProps) {
  const { t } = useTranslation();
  const items = usePlanItems();
  const removeItem = usePlanStore((state) => state.removeItem);
  const updateItemAlt = usePlanStore((state) => state.updateItemAlt);
  const loadFrom = usePlanStore((state) => state.loadFrom);

  const onVehicleMission = useMission(vehicleId);
  const { request, sendUpload, sendClear } = useMissionRequest(client);

  const pending = request?.phase === "pending";

  const handleLoadFromVehicle = useCallback(() => {
    loadFrom(onVehicleMission?.items ?? []);
  }, [loadFrom, onVehicleMission]);

  const handleUpload = useCallback(() => {
    sendUpload(vehicleId, items);
  }, [sendUpload, vehicleId, items]);

  const handleClearVehicle = useCallback(() => {
    sendClear(vehicleId);
  }, [sendClear, vehicleId]);

  return (
    <div className="waypoint-list" aria-label="Mission plan">
      {request && (
        <div className={`waypoint-list-status waypoint-list-status--${request.phase}`} role="status">
          {request.kind === "upload" ? (
            request.phase === "pending" ? (
              t("Uploading…")
            ) : request.phase === "accepted" ? (
              `${t("Upload accepted")} (${request.itemCount ?? items.length} ${t("items")})`
            ) : (
              `${t("Upload rejected")}: ${request.reason ?? t("unknown reason")}`
            )
          ) : request.phase === "pending" ? (
            t("Clearing vehicle mission…")
          ) : request.phase === "accepted" ? (
            t("Vehicle mission cleared")
          ) : (
            `${t("Clear rejected")}: ${request.reason ?? t("unknown reason")}`
          )}
        </div>
      )}

      <div className="waypoint-list-items">
        {items.length === 0 ? (
          <div className="waypoint-list-empty">
            {t("No draft waypoints — switch to PLAN and click the map, or load the vehicle's mission.")}
          </div>
        ) : (
          items.map((item) => (
            <div key={item.seq} className="waypoint-list-row">
              <span className="waypoint-list-seq">#{item.seq}</span>
              <span className="waypoint-list-coords">
                {item.lat.toFixed(6)}, {item.lon.toFixed(6)}
              </span>
              <label className="waypoint-list-alt-row">
                <input
                  type="number"
                  step="1"
                  className="waypoint-list-alt-input"
                  value={item.alt}
                  onChange={(event) => {
                    const value = Number(event.target.value);
                    if (Number.isFinite(value)) {
                      updateItemAlt(item.seq, value);
                    }
                  }}
                  aria-label={`Altitude for waypoint ${item.seq}, meters`}
                />
                <span className="waypoint-list-alt-unit">m</span>
              </label>
              <button
                type="button"
                className="waypoint-list-remove"
                aria-label={`Remove waypoint ${item.seq}`}
                onClick={() => removeItem(item.seq)}
              >
                ✕
              </button>
            </div>
          ))
        )}
      </div>

      <div className="waypoint-list-actions">
        <button
          type="button"
          className="waypoint-list-action"
          onClick={handleLoadFromVehicle}
          disabled={!onVehicleMission}
        >
          {t("Load from vehicle")}
        </button>
        <button
          type="button"
          className="waypoint-list-action waypoint-list-action--accent"
          onClick={handleUpload}
          disabled={pending}
        >
          {pending && request?.kind === "upload" ? t("Uploading…") : t("Upload")}
        </button>
        <button
          type="button"
          className="waypoint-list-action waypoint-list-action--critical"
          onClick={handleClearVehicle}
          disabled={pending}
        >
          {pending && request?.kind === "clear" ? t("Clearing…") : t("Clear vehicle")}
        </button>
      </div>
    </div>
  );
}
