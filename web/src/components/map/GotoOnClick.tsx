/**
 * Click-to-goto map layer — click the map to drop a candidate marker with a
 * confirm popover (coords + altitude), then send a `gotoLocation` guided
 * command (PROTOCOL.md §5.1, params `{lat, lon, alt}`, alt meters RELATIVE).
 *
 * Only active when the vehicle exists, is armed, and the bridge connection
 * is live — otherwise clicks are ignored and the map cursor stays default.
 * Reuses `useCommandRequests` (same request-tracking hook as ActionsPanel)
 * so pending/accepted/rejected phases come from the same ack/progress
 * machinery, keyed by this component's own request id.
 *
 * Renders no React DOM itself — the candidate marker and confirm popover are
 * plain maplibre/DOM objects managed imperatively via refs, matching
 * VehicleLayer's pattern.
 */

import { useCallback, useEffect, useRef, useState } from "react";
import maplibregl from "maplibre-gl";
import type { Map as MapLibreMap, MapMouseEvent } from "maplibre-gl";

import type { BridgeClient } from "../../bridge/BridgeClient.ts";
import { useCommandRequests } from "../actions/useCommandRequests.ts";
import { useConnection, useVehicle } from "../../store/index.ts";
import "./goto-on-click.css";

export interface GotoOnClickProps {
  map: MapLibreMap | null;
  client: BridgeClient;
  vehicleId: number;
}

/** How long a rejected request's reason stays visible near the marker before clearing. */
const REJECTED_DISPLAY_MS = 5000;
/** Floor for the altitude input — matches the ActionsPanel takeoff-alt convention. */
const MIN_ALT_M = 1;

interface Candidate {
  lat: number;
  lon: number;
  /** Altitude prefill at click time (vehicle's current altRel, rounded, min 1). */
  altitude: number;
}

interface MarkerHandle {
  marker: maplibregl.Marker;
  el: HTMLDivElement;
}

interface PopoverHandle {
  popup: maplibregl.Popup;
  root: HTMLDivElement;
  altInput: HTMLInputElement;
  goBtn: HTMLButtonElement;
  cancelBtn: HTMLButtonElement;
  status: HTMLDivElement;
}

/** Builds the candidate marker's DOM element: dashed ring + crosshair, distinct from the vehicle marker. */
function createCandidateMarkerElement(): HTMLDivElement {
  const el = document.createElement("div");
  el.className = "goto-marker";
  el.innerHTML = `
    <svg width="32" height="32" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg">
      <circle class="goto-marker-ring" cx="16" cy="16" r="10" />
      <line class="goto-marker-cross" x1="16" y1="1" x2="16" y2="9" />
      <line class="goto-marker-cross" x1="16" y1="23" x2="16" y2="31" />
      <line class="goto-marker-cross" x1="1" y1="16" x2="9" y2="16" />
      <line class="goto-marker-cross" x1="23" y1="16" x2="31" y2="16" />
    </svg>
    <span class="goto-marker-spinner" aria-hidden="true"></span>
  `;
  return el;
}

/** Builds the confirm popover's DOM content (coords, altitude input, GO/CANCEL, status line). */
function createPopoverContent(): {
  root: HTMLDivElement;
  coords: HTMLDivElement;
  altInput: HTMLInputElement;
  goBtn: HTMLButtonElement;
  cancelBtn: HTMLButtonElement;
  status: HTMLDivElement;
} {
  const root = document.createElement("div");
  root.className = "goto-popover";

  const coords = document.createElement("div");
  coords.className = "goto-popover-coords";
  root.appendChild(coords);

  const altRow = document.createElement("label");
  altRow.className = "goto-popover-alt-row";
  const altLabel = document.createElement("span");
  altLabel.textContent = "Alt (m rel)";
  const altInput = document.createElement("input");
  altInput.type = "number";
  altInput.min = String(MIN_ALT_M);
  altInput.step = "1";
  altInput.className = "goto-popover-alt-input";
  altRow.append(altLabel, altInput);
  root.appendChild(altRow);

  const actions = document.createElement("div");
  actions.className = "goto-popover-actions";
  const goBtn = document.createElement("button");
  goBtn.type = "button";
  goBtn.className = "goto-popover-go";
  goBtn.textContent = "GO";
  const cancelBtn = document.createElement("button");
  cancelBtn.type = "button";
  cancelBtn.className = "goto-popover-cancel";
  cancelBtn.textContent = "CANCEL";
  actions.append(goBtn, cancelBtn);
  root.appendChild(actions);

  const status = document.createElement("div");
  status.className = "goto-popover-status";
  status.hidden = true;
  root.appendChild(status);

  return { root, coords, altInput, goBtn, cancelBtn, status };
}

export function GotoOnClick({ map, client, vehicleId }: GotoOnClickProps) {
  const vehicle = useVehicle(vehicleId);
  const connectionState = useConnection((state) => state.state);
  const { requests, send } = useCommandRequests(client);

  const [candidate, setCandidate] = useState<Candidate | null>(null);
  const [requestId, setRequestId] = useState<string | null>(null);

  const active = vehicle !== undefined && vehicle.armed && connectionState === "connected";
  const request = requestId ? requests[requestId] : undefined;

  const markerRef = useRef<MarkerHandle | null>(null);
  const popoverRef = useRef<PopoverHandle | null>(null);

  // Refs so the stable click handler always sees current gating/prefill
  // values without the listener being re-registered on every telemetry tick.
  const activeRef = useRef(active);
  activeRef.current = active;
  const altRelRef = useRef(vehicle?.position.altRel ?? MIN_ALT_M);
  altRelRef.current = vehicle?.position.altRel ?? MIN_ALT_M;
  const pendingRef = useRef(false);
  pendingRef.current = request?.phase === "pending";

  const clearCandidate = useCallback(() => {
    setCandidate(null);
    setRequestId(null);
  }, []);

  // Stable click handler identity (required so map.off in cleanup targets
  // the exact same function reference registered via map.on).
  const handleClick = useCallback((event: MapMouseEvent) => {
    if (!activeRef.current || pendingRef.current) {
      return;
    }
    const { lat, lng } = event.lngLat;
    const altitude = Math.max(MIN_ALT_M, Math.round(altRelRef.current));
    setCandidate({ lat, lon: lng, altitude });
    setRequestId(null);
  }, []);

  const handleKeyDown = useCallback(
    (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        clearCandidate();
      }
    },
    [clearCandidate],
  );

  // Register the map click listener once the style is ready; guard null map.
  useEffect(() => {
    if (!map) return;

    let disposed = false;
    const register = () => {
      if (disposed) return;
      map.on("click", handleClick);
    };

    if (map.isStyleLoaded()) {
      register();
    } else {
      map.once("load", register);
    }

    return () => {
      disposed = true;
      map.off("load", register);
      map.off("click", handleClick);
    };
  }, [map, handleClick]);

  // ESC clears the candidate regardless of map focus.
  useEffect(() => {
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [handleKeyDown]);

  // Crosshair cursor while the layer is active (armed + live).
  useEffect(() => {
    if (!map) return;
    const canvas = map.getCanvas();
    const previousCursor = canvas.style.cursor;
    if (active) {
      canvas.style.cursor = "crosshair";
    }
    return () => {
      if (canvas.style.cursor === "crosshair") {
        canvas.style.cursor = previousCursor === "crosshair" ? "" : previousCursor;
      }
    };
  }, [map, active]);

  // If the layer stops being active mid-flow (disarm/disconnect), drop any candidate.
  useEffect(() => {
    if (!active && candidate) {
      clearCandidate();
    }
  }, [active, candidate, clearCandidate]);

  // Create/destroy the marker + popover DOM, keyed on the candidate's position.
  // eslint-disable-next-line react-hooks/exhaustive-deps
  useEffect(() => {
    if (!map || !candidate) return;

    const markerEl = createCandidateMarkerElement();
    const marker = new maplibregl.Marker({ element: markerEl, anchor: "center" })
      .setLngLat([candidate.lon, candidate.lat])
      .addTo(map);
    markerRef.current = { marker, el: markerEl };

    const { root, coords, altInput, goBtn, cancelBtn, status } = createPopoverContent();
    coords.textContent = `${candidate.lat.toFixed(5)}, ${candidate.lon.toFixed(5)}`;
    altInput.value = String(candidate.altitude);

    const popup = new maplibregl.Popup({
      closeButton: false,
      closeOnClick: false,
      anchor: "bottom",
      offset: 20,
      className: "goto-popup",
    })
      .setLngLat([candidate.lon, candidate.lat])
      .setDOMContent(root)
      .addTo(map);
    popoverRef.current = { popup, root, altInput, goBtn, cancelBtn, status };

    const onGo = () => {
      const raw = Number(altInput.value);
      const alt = Number.isFinite(raw) && raw >= MIN_ALT_M ? Math.round(raw) : MIN_ALT_M;
      const { id, ok } = send(vehicleId, "gotoLocation", { lat: candidate.lat, lon: candidate.lon, alt });
      if (ok) {
        setRequestId(id);
      }
    };
    const onCancel = () => clearCandidate();

    goBtn.addEventListener("click", onGo);
    cancelBtn.addEventListener("click", onCancel);

    return () => {
      goBtn.removeEventListener("click", onGo);
      cancelBtn.removeEventListener("click", onCancel);
      marker.remove();
      popup.remove();
      markerRef.current = null;
      popoverRef.current = null;
    };
  }, [map, candidate]);

  // React to the tracked request's phase: spinner while pending, clear on
  // accepted, show reason near the marker for ~5s then clear on rejected.
  useEffect(() => {
    const markerHandle = markerRef.current;
    const popoverHandle = popoverRef.current;
    if (!markerHandle || !popoverHandle) return;

    const phase = request?.phase;
    const { altInput, goBtn, cancelBtn, status } = popoverHandle;

    markerHandle.el.classList.toggle("goto-marker--pending", phase === "pending");

    if (phase === "pending") {
      altInput.disabled = true;
      goBtn.disabled = true;
      cancelBtn.disabled = true;
      status.hidden = true;
    } else if (phase === "rejected") {
      altInput.hidden = true;
      goBtn.hidden = true;
      cancelBtn.hidden = true;
      status.hidden = false;
      status.textContent = request?.reason ?? "Rejected";
      status.classList.add("goto-popover-status--error");
    } else {
      altInput.disabled = false;
      altInput.hidden = false;
      goBtn.disabled = false;
      goBtn.hidden = false;
      cancelBtn.disabled = false;
      cancelBtn.hidden = false;
      status.hidden = true;
      status.classList.remove("goto-popover-status--error");
    }
  }, [request?.phase, request?.reason]);

  // Accepted clears immediately; rejected clears after the reason has been shown.
  useEffect(() => {
    if (!requestId) return;
    if (request?.phase === "accepted") {
      clearCandidate();
    } else if (request?.phase === "rejected") {
      const timer = setTimeout(clearCandidate, REJECTED_DISPLAY_MS);
      return () => clearTimeout(timer);
    }
  }, [requestId, request?.phase, clearCandidate]);

  return null;
}
