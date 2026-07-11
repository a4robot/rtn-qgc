/**
 * Click-to-add-waypoint map layer, PROTOCOL.md §7 — the plan-mode
 * counterpart to GotoOnClick. When `uiStore.mapMode === "plan"`, a map click
 * appends a draft NAV_WAYPOINT (§7.1: frame 6 GLOBAL_RELATIVE_ALT_INT,
 * command 16 NAV_WAYPOINT, default altitude) to `planStore` instead of
 * sending a `gotoLocation` guided command. In "fly" mode this component is
 * inert — GotoOnClick owns the click in that mode.
 *
 * Also draws the draft mission on the map as its own source/layers, in a
 * color distinct from both the on-vehicle mission (MissionLayer, amber
 * `#d29922`) and the vehicle trail — solid blue `#58a6ff`, so a user editing
 * a draft next to an existing on-vehicle mission can tell them apart at a
 * glance.
 *
 * Renders no React DOM itself — all map resources are managed imperatively
 * via refs/effects, matching MissionLayer/VehicleLayer's pattern.
 */

import { useCallback, useEffect, useRef, useState } from "react";
import type { GeoJSONSource, Map as MapLibreMap, MapMouseEvent } from "maplibre-gl";
import type { Feature, FeatureCollection, LineString, Point } from "geojson";

import type { BridgeClient } from "../../bridge/BridgeClient.ts";
import type { MissionItem } from "../../bridge/types.ts";
import { useMapMode, usePlanItems, usePlanStore } from "../../store/index.ts";

export interface WaypointAdderProps {
  map: MapLibreMap | null;
  /** Unused by this component today (no bridge messages are sent while drafting); kept for signature parity with other map layers. */
  client: BridgeClient;
  vehicleId: number;
}

// Renders on the maplibre canvas, which carries the Ingress dark-map CSS
// filter (see map/map.css) — like MissionLayer's WAYPOINT_COLOR/
// CURRENT_COLOR, this is a base hex chosen so it lands on a vivid color
// *after* that filter runs, not the color it visually is here. Picked from
// the Ingress secondary/magenta family so a draft mission reads as a
// distinct "editing" accent against MissionLayer's on-vehicle amber-orange
// and the vehicle trail's blue/red.
const DRAFT_COLOR = "#e620e6"; // -> vivid magenta once the map filter runs

function emptyLineData(): FeatureCollection<LineString> {
  return {
    type: "FeatureCollection",
    features: [
      {
        type: "Feature",
        properties: {},
        geometry: { type: "LineString", coordinates: [] },
      } satisfies Feature<LineString>,
    ],
  };
}

function emptyPointData(): FeatureCollection<Point> {
  return { type: "FeatureCollection", features: [] };
}

function pointData(items: MissionItem[]): FeatureCollection<Point> {
  return {
    type: "FeatureCollection",
    features: items.map(
      (item) =>
        ({
          type: "Feature",
          properties: { seq: item.seq },
          geometry: { type: "Point", coordinates: [item.lon, item.lat] },
        }) satisfies Feature<Point>,
    ),
  };
}

function lineData(items: MissionItem[]): FeatureCollection<LineString> {
  return {
    type: "FeatureCollection",
    features: [
      {
        type: "Feature",
        properties: {},
        geometry: {
          type: "LineString",
          coordinates: items.map((item) => [item.lon, item.lat]),
        },
      } satisfies Feature<LineString>,
    ],
  };
}

export function WaypointAdder({ map, vehicleId }: WaypointAdderProps) {
  const mapMode = useMapMode();
  const items = usePlanItems();

  const lineSourceId = `waypoint-adder-${vehicleId}-line`;
  const lineLayerId = `waypoint-adder-${vehicleId}-line-layer`;
  const pointSourceId = `waypoint-adder-${vehicleId}-points`;
  const circleLayerId = `waypoint-adder-${vehicleId}-points-circle`;
  const labelLayerId = `waypoint-adder-${vehicleId}-points-label`;

  // Layer readiness is state, not a ref: draft items can exist (e.g. after
  // loadFrom) before the map style finishes loading, so the data effect
  // below must re-run once setup completes — see MissionLayer for the same
  // reasoning.
  const [ready, setReady] = useState(false);

  // --- Setup / teardown of sources + layers, keyed on the map instance. ---
  useEffect(() => {
    if (!map) return;

    let cancelled = false;

    const setup = () => {
      if (cancelled) return;

      if (!map.getSource(lineSourceId)) {
        map.addSource(lineSourceId, { type: "geojson", data: emptyLineData() });
      }
      if (!map.getLayer(lineLayerId)) {
        map.addLayer({
          id: lineLayerId,
          type: "line",
          source: lineSourceId,
          layout: { "line-join": "round", "line-cap": "round" },
          paint: {
            "line-color": DRAFT_COLOR,
            "line-width": 2,
            "line-opacity": 0.9,
          },
        });
      }

      if (!map.getSource(pointSourceId)) {
        map.addSource(pointSourceId, { type: "geojson", data: emptyPointData() });
      }
      if (!map.getLayer(circleLayerId)) {
        map.addLayer({
          id: circleLayerId,
          type: "circle",
          source: pointSourceId,
          paint: {
            "circle-radius": 6,
            "circle-color": DRAFT_COLOR,
            "circle-stroke-color": "rgba(13,17,23,0.85)",
            "circle-stroke-width": 1.5,
          },
        });
      }
      if (!map.getLayer(labelLayerId)) {
        map.addLayer({
          id: labelLayerId,
          type: "symbol",
          source: pointSourceId,
          layout: {
            "text-field": ["to-string", ["get", "seq"]],
            "text-size": 10,
            "text-allow-overlap": true,
            "text-ignore-placement": true,
          },
          paint: {
            "text-color": "#0d1117",
          },
        });
      }

      setReady(true);
    };

    // The map instance arrives after its own "load" event has already fired
    // (CoreMap hands it out from inside its once("load") handler), so
    // waiting on "load" here would deadlock. isStyleLoaded() can still
    // report false while tiles are in flight; retry on "idle", which fires
    // after every loading burst. Copied from MissionLayer's trySetup.
    const trySetup = () => {
      if (cancelled) return;
      if (map.isStyleLoaded()) {
        setup();
      } else {
        map.once("idle", trySetup);
      }
    };
    trySetup();

    return () => {
      cancelled = true;
      setReady(false);
      map.off("idle", trySetup);

      if (map.getLayer(labelLayerId)) map.removeLayer(labelLayerId);
      if (map.getLayer(circleLayerId)) map.removeLayer(circleLayerId);
      if (map.getLayer(lineLayerId)) map.removeLayer(lineLayerId);
      if (map.getSource(pointSourceId)) map.removeSource(pointSourceId);
      if (map.getSource(lineSourceId)) map.removeSource(lineSourceId);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [map, lineSourceId, lineLayerId, pointSourceId, circleLayerId, labelLayerId]);

  // --- React to draft item changes: update the line + point sources. ---
  useEffect(() => {
    if (!map || !ready) return;

    const lineSource = map.getSource(lineSourceId) as GeoJSONSource | undefined;
    lineSource?.setData(lineData(items));

    const pointSource = map.getSource(pointSourceId) as GeoJSONSource | undefined;
    pointSource?.setData(pointData(items));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [map, ready, items, lineSourceId, pointSourceId]);

  // Stable click handler identity (required so map.off in cleanup targets
  // the exact same function reference registered via map.on). Reads
  // mapMode via a ref so re-registration isn't needed on every mode flip.
  const mapModeRef = useRef(mapMode);
  mapModeRef.current = mapMode;

  const handleClick = useCallback((event: MapMouseEvent) => {
    if (mapModeRef.current !== "plan") {
      return;
    }
    const { lat, lng } = event.lngLat;
    usePlanStore.getState().addWaypoint(lat, lng);
  }, []);

  // Click registration doesn't depend on the style/layers being ready —
  // attaching a listener to the map instance works as soon as it exists.
  useEffect(() => {
    if (!map) return;
    map.on("click", handleClick);
    return () => {
      map.off("click", handleClick);
    };
  }, [map, handleClick]);

  // Crosshair cursor while plan mode is active, matching GotoOnClick's cue.
  useEffect(() => {
    if (!map) return;
    const canvas = map.getCanvas();
    const previousCursor = canvas.style.cursor;
    if (mapMode === "plan") {
      canvas.style.cursor = "crosshair";
    }
    return () => {
      if (canvas.style.cursor === "crosshair") {
        canvas.style.cursor = previousCursor === "crosshair" ? "" : previousCursor;
      }
    };
  }, [map, mapMode]);

  return null;
}
