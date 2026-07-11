import { useEffect, useRef } from "react";
import maplibregl from "maplibre-gl";
import type { Map as MapLibreMap, GeoJSONSource } from "maplibre-gl";
import type { Feature, FeatureCollection, LineString } from "geojson";

import { useVehicle } from "../../store/index.ts";

export interface VehicleLayerProps {
  map: MapLibreMap | null;
  vehicleId: number;
  /** Ease the map toward the vehicle as it moves. Default false. */
  follow?: boolean;
}

/** Max positions retained in the trail ring buffer. */
const TRAIL_MAX_POINTS = 500;

/** Minimum movement (meters) before a follow re-center is triggered. */
const FOLLOW_THRESHOLD_M = 3;

// Mirrors app.css's --status-critical/--accent. Can't reference the CSS
// vars directly: below, these feed both the marker icon's SVG `fill`
// attribute *and* a maplibre GL `line-color` paint property (the trail) —
// maplibre parses paint colors with its own color parser rather than the
// browser's CSSOM, so `var(...)` would never resolve there, and both use
// sites share these constants.
//
// Only the marker icon is exempt from the Ingress dark-map canvas filter
// (see map/map.css) — it's a DOM overlay (maplibregl.Marker), not part of
// the filtered `.maplibregl-canvas`, so it always shows this true color.
// The trail *is* a GL layer drawn onto that same canvas, so it does get
// filtered — checked against the filter recipe, #ff3b46/#12e0ea still land
// on a clearly-visible orange-red/blue post-filter, so no compensation
// needed there (unlike MissionLayer's waypoint/current colors).
const ARMED_COLOR = "#ff3b46"; // --status-critical
const DISARMED_COLOR = "#12e0ea"; // --accent

/** Haversine distance in meters between two [lon, lat] points. */
function distanceMeters(a: [number, number], b: [number, number]): number {
  const R = 6371000;
  const [lon1, lat1] = a;
  const [lon2, lat2] = b;
  const rad = Math.PI / 180;
  const dLat = (lat2 - lat1) * rad;
  const dLon = (lon2 - lon1) * rad;
  const sinLat = Math.sin(dLat / 2);
  const sinLon = Math.sin(dLon / 2);
  const h =
    sinLat * sinLat +
    Math.cos(lat1 * rad) * Math.cos(lat2 * rad) * sinLon * sinLon;
  return 2 * R * Math.asin(Math.sqrt(h));
}

/** Builds the vehicle marker's SVG element (an aircraft/quad arrow icon). */
function createMarkerElement(): { el: HTMLDivElement; path: SVGPathElement } {
  const el = document.createElement("div");
  el.className = "vehicle-marker";
  el.style.width = "28px";
  el.style.height = "28px";
  el.style.display = "flex";
  el.style.alignItems = "center";
  el.style.justifyContent = "center";

  el.innerHTML = `
    <svg width="28" height="28" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
      <path
        d="M12 2 L19 20 L12 16 L5 20 Z"
        stroke="rgba(10,18,22,0.85)"
        stroke-width="1.5"
        stroke-linejoin="round"
      />
    </svg>
  `;

  const path = el.querySelector("path");
  if (!path) throw new Error("VehicleLayer: marker svg path missing");
  return { el, path };
}

function emptyTrailData(): FeatureCollection<LineString> {
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

/**
 * Renders a vehicle marker (rotated to heading, colored by armed state) and
 * its recent-position trail on a maplibre map. Renders no DOM itself — all
 * map resources are managed imperatively via refs.
 */
export function VehicleLayer({ map, vehicleId, follow = false }: VehicleLayerProps) {
  const vehicle = useVehicle(vehicleId);

  const sourceId = `vehicle-${vehicleId}-trail`;
  const layerId = `vehicle-${vehicleId}-trail-layer`;

  const markerRef = useRef<maplibregl.Marker | null>(null);
  const iconPathRef = useRef<SVGPathElement | null>(null);
  const readyRef = useRef(false);
  const trailRef = useRef<[number, number][]>([]);
  const lastFollowPosRef = useRef<[number, number] | null>(null);
  const addedToMapRef = useRef(false);

  // --- Setup / teardown of marker + trail source+layer, keyed on the map instance. ---
  useEffect(() => {
    if (!map) return;

    let cancelled = false;

    const setup = () => {
      if (cancelled) return;

      const { el, path } = createMarkerElement();
      iconPathRef.current = path;
      path.setAttribute("fill", DISARMED_COLOR);

      const marker = new maplibregl.Marker({
        element: el,
        rotationAlignment: "map",
        pitchAlignment: "map",
      });
      markerRef.current = marker;

      if (!map.getSource(sourceId)) {
        map.addSource(sourceId, {
          type: "geojson",
          data: emptyTrailData(),
        });
      }
      if (!map.getLayer(layerId)) {
        map.addLayer({
          id: layerId,
          type: "line",
          source: sourceId,
          layout: { "line-join": "round", "line-cap": "round" },
          paint: {
            "line-color": DISARMED_COLOR,
            "line-width": 2,
            "line-opacity": 0.5,
          },
        });
      }

      readyRef.current = true;
    };

    // Same pitfall as MissionLayer: the map arrives post-"load" (CoreMap
    // calls onMapReady from once("load")), so gate on isStyleLoaded() with an
    // "idle" retry — never on the already-spent "load" event.
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
      readyRef.current = false;
      map.off("idle", trySetup);

      markerRef.current?.remove();
      markerRef.current = null;
      iconPathRef.current = null;
      addedToMapRef.current = false;

      if (map.getLayer(layerId)) map.removeLayer(layerId);
      if (map.getSource(sourceId)) map.removeSource(sourceId);

      trailRef.current = [];
      lastFollowPosRef.current = null;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [map, sourceId, layerId]);

  // --- React to vehicle state changes: marker position/rotation/color, trail, follow. ---
  useEffect(() => {
    if (!map || !readyRef.current) return;

    const marker = markerRef.current;
    if (!marker) return;

    const lat = vehicle?.position.lat;
    const lon = vehicle?.position.lon;
    const hasFix = vehicle !== undefined && !!lat && !!lon;

    if (!hasFix) {
      marker.getElement().style.display = "none";
      return;
    }

    const pos: [number, number] = [lon, lat];

    marker.getElement().style.display = "";
    marker.setLngLat(pos);
    marker.setRotation(vehicle.attitude.yaw ?? 0);
    marker.getElement().style.opacity = vehicle.connected ? "1" : "0.4";

    const path = iconPathRef.current;
    if (path) {
      path.setAttribute("fill", vehicle.armed ? ARMED_COLOR : DISARMED_COLOR);
    }
    if (map.getLayer(layerId)) {
      map.setPaintProperty(
        layerId,
        "line-color",
        vehicle.armed ? ARMED_COLOR : DISARMED_COLOR,
      );
    }

    if (!addedToMapRef.current) {
      marker.addTo(map);
      addedToMapRef.current = true;
    }

    // Trail ring buffer.
    const trail = trailRef.current;
    const last = trail[trail.length - 1];
    if (!last || last[0] !== pos[0] || last[1] !== pos[1]) {
      trail.push(pos);
      if (trail.length > TRAIL_MAX_POINTS) {
        trail.splice(0, trail.length - TRAIL_MAX_POINTS);
      }
      const source = map.getSource(sourceId) as GeoJSONSource | undefined;
      source?.setData({
        type: "FeatureCollection",
        features: [
          {
            type: "Feature",
            properties: {},
            geometry: { type: "LineString", coordinates: trail },
          },
        ],
      });
    }

    // Follow.
    if (follow) {
      const lastFollowPos = lastFollowPosRef.current;
      if (!lastFollowPos || distanceMeters(lastFollowPos, pos) > FOLLOW_THRESHOLD_M) {
        lastFollowPosRef.current = pos;
        map.easeTo({ center: pos, duration: 500 });
      }
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [map, vehicle, follow, sourceId, layerId]);

  return null;
}
