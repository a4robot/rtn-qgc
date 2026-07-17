import { useEffect, useState } from "react";
import maplibregl from "maplibre-gl";
import type { Map as MapLibreMap, GeoJSONSource } from "maplibre-gl";
import type { Feature, FeatureCollection, LineString, Point } from "geojson";

import { useMission } from "../../store/index.ts";
import type { MissionItem } from "../../bridge/types.ts";

export interface MissionLayerProps {
  map: MapLibreMap | null;
  vehicleId: number;
}

/**
 * `MAV_CMD` ids (PROTOCOL.md §7.1) for NAV_* mission items that carry a
 * meaningful waypoint — the NAV_WAYPOINT family. Non-spatial commands (e.g.
 * `DO_SET_SERVO`, `DO_CHANGE_SPEED`) are never in this set and are always
 * excluded regardless of their lat/lon.
 */
const SPATIAL_NAV_COMMANDS = new Set([
  16, // NAV_WAYPOINT
  17, // NAV_LOITER_UNLIM
  18, // NAV_LOITER_TURNS
  19, // NAV_LOITER_TIME
  20, // NAV_RETURN_TO_LAUNCH
  21, // NAV_LAND
  22, // NAV_TAKEOFF
  82, // NAV_SPLINE_WAYPOINT
  84, // NAV_VTOL_TAKEOFF
  85, // NAV_VTOL_LAND
]);

/**
 * An item is drawable when it's a NAV_WAYPOINT-family command *and* carries a
 * real coordinate — some autopilots emit e.g. RTL with lat/lon 0,0 (computed
 * onboard from home position), which has nothing sensible to plot.
 */
function isSpatialItem(item: MissionItem): boolean {
  return SPATIAL_NAV_COMMANDS.has(item.command) && (item.lat !== 0 || item.lon !== 0);
}

// Direct on-screen colors. (The old canvas-wide dark-map CSS filter is
// gone — the basemap is a dark vector style now — so these are no longer
// pre-compensated for a filter pipeline; what you write is what renders.)
const WAYPOINT_COLOR = "#ffa733"; // vivid amber-orange
const CURRENT_COLOR = "#31d158"; // vivid green

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

function pointData(items: MissionItem[], currentSeq: number): FeatureCollection<Point> {
  const spatial = items.filter(isSpatialItem).sort((a, b) => a.seq - b.seq);
  return {
    type: "FeatureCollection",
    features: spatial.map(
      (item) =>
        ({
          type: "Feature",
          properties: { seq: item.seq, current: item.seq === currentSeq },
          geometry: { type: "Point", coordinates: [item.lon, item.lat] },
        }) satisfies Feature<Point>,
    ),
  };
}

function lineData(items: MissionItem[]): FeatureCollection<LineString> {
  const spatial = items.filter(isSpatialItem).sort((a, b) => a.seq - b.seq);
  return {
    type: "FeatureCollection",
    features: [
      {
        type: "Feature",
        properties: {},
        geometry: {
          type: "LineString",
          coordinates: spatial.map((item) => [item.lon, item.lat]),
        },
      } satisfies Feature<LineString>,
    ],
  };
}

/**
 * Renders the on-vehicle mission as numbered waypoint markers connected by a
 * dashed route line (visually distinct from VehicleLayer's solid trail), with
 * the item the vehicle is currently flying to (`currentSeq`) highlighted.
 * Renders no React DOM itself — all map resources are managed imperatively
 * via refs, matching VehicleLayer's pattern.
 */
export function MissionLayer({ map, vehicleId }: MissionLayerProps) {
  const mission = useMission(vehicleId);

  const lineSourceId = `mission-${vehicleId}-line`;
  const lineLayerId = `mission-${vehicleId}-line-layer`;
  const pointSourceId = `mission-${vehicleId}-points`;
  const circleLayerId = `mission-${vehicleId}-points-circle`;
  const labelLayerId = `mission-${vehicleId}-points-label`;

  // Layer readiness is state, not a ref: the mission snapshot usually arrives
  // (once) before the map style finishes loading, so the data effect below
  // must re-run when setup completes — a ref flip wouldn't trigger it and the
  // one-shot mission would never be drawn.
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
            "line-color": WAYPOINT_COLOR,
            "line-width": 2,
            "line-dasharray": [2, 2],
            "line-opacity": 0.85,
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
            "circle-radius": ["case", ["get", "current"], 8, 6],
            "circle-color": ["case", ["get", "current"], CURRENT_COLOR, WAYPOINT_COLOR],
            // Light ring + light seq number keep the marker legible against
            // both the near-black land and the saturated-blue water fills.
            "circle-stroke-color": "rgba(238,242,244,0.9)",
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
            "text-color": "#f4f7f8",
          },
        });
      }

      setReady(true);
    };

    // CoreMap delivers the map from inside its own once("load") handler, so
    // "load" has already fired and will never fire again — waiting on it here
    // would deadlock. isStyleLoaded() can still report false while tiles are
    // in flight; retry on "idle", which fires after every loading burst.
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

  // --- React to mission state changes: update the line + point sources. ---
  useEffect(() => {
    if (!map || !ready) return;

    const items = mission?.items ?? [];
    const currentSeq = mission?.currentSeq ?? -1;

    const lineSource = map.getSource(lineSourceId) as GeoJSONSource | undefined;
    lineSource?.setData(lineData(items));

    const pointSource = map.getSource(pointSourceId) as GeoJSONSource | undefined;
    pointSource?.setData(pointData(items, currentSeq));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [map, ready, mission, lineSourceId, pointSourceId]);

  return null;
}
