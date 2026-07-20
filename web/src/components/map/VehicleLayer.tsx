import { useEffect, useRef } from "react";
import maplibregl from "maplibre-gl";
import type { Map as MapLibreMap, GeoJSONSource } from "maplibre-gl";
import type { Feature, FeatureCollection, LineString } from "geojson";

import { useVehicle, useVehicleMarkerStyle } from "../../store/index.ts";

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
// Both the DOM marker and the GL trail render these true colors directly —
// the old canvas-wide dark-map CSS filter is gone (the basemap is a dark
// vector style now), so no filter compensation applies anywhere on the map.
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

function createMarkerElement(): { el: HTMLDivElement; path: SVGPathElement; ringEl: HTMLDivElement; goldRingEl: HTMLDivElement } {
  const el = document.createElement("div");
  el.className = "vehicle-marker";
  el.style.width = "28px";
  el.style.height = "28px";
  el.style.display = "flex";
  el.style.alignItems = "center";
  el.style.justifyContent = "center";
  
  if (!document.getElementById("sonar-ping-style")) {
    const style = document.createElement("style");
    style.id = "sonar-ping-style";
    style.textContent = `
      @keyframes sonar-ping {
        0% {
          transform: translate(-50%, -50%) scale(0);
          opacity: 0.8;
        }
        100% {
          transform: translate(-50%, -50%) scale(1);
          opacity: 0;
        }
      }
    `;
    document.head.appendChild(style);
  }

  const ringEl = document.createElement("div");
  ringEl.className = "vehicle-ring";
  ringEl.style.position = "absolute";
  ringEl.style.top = "50%";
  ringEl.style.left = "50%";
  ringEl.style.transform = "translate(-50%, -50%)";
  ringEl.style.border = "2px solid rgba(255, 255, 255, 0.8)";
  ringEl.style.boxShadow = "0 0 15px rgba(255, 255, 255, 0.6), inset 0 0 15px rgba(255, 255, 255, 0.3)";
  ringEl.style.borderRadius = "50%";
  ringEl.style.pointerEvents = "none";
  ringEl.style.display = "none"; // Off by default
  ringEl.style.boxSizing = "border-box";
  ringEl.style.animation = "sonar-ping 3s cubic-bezier(0, 0, 0.2, 1) infinite";
  el.appendChild(ringEl);

  const goldRingEl = document.createElement("div");
  goldRingEl.className = "vehicle-gold-ring";
  goldRingEl.style.position = "absolute";
  goldRingEl.style.top = "50%";
  goldRingEl.style.left = "50%";
  goldRingEl.style.transform = "translate(-50%, -50%)";
  goldRingEl.style.border = "2px solid rgba(255, 215, 0, 0.8)";
  goldRingEl.style.boxShadow = "0 0 10px rgba(255, 215, 0, 0.5), inset 0 0 10px rgba(255, 215, 0, 0.3)";
  goldRingEl.style.borderRadius = "50%";
  goldRingEl.style.pointerEvents = "none";
  goldRingEl.style.display = "none"; // Off by default
  goldRingEl.style.boxSizing = "border-box";
  el.appendChild(goldRingEl);

  const svgWrapper = document.createElement("div");
  svgWrapper.style.position = "absolute";
  svgWrapper.style.display = "flex";
  svgWrapper.style.alignItems = "center";
  svgWrapper.style.justifyContent = "center";
  svgWrapper.innerHTML = `
    <svg width="28" height="28" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
      <path
        d="M12 2 L19 20 L12 16 L5 20 Z"
        stroke="rgba(10,18,22,0.85)"
        stroke-width="1.5"
        stroke-linejoin="round"
      />
    </svg>
  `;
  el.appendChild(svgWrapper);

  const path = svgWrapper.querySelector("path");
  if (!path) throw new Error("VehicleLayer: marker svg path missing");
  return { el, path, ringEl, goldRingEl };
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
  const markerStyle = useVehicleMarkerStyle();

  const sourceId = `vehicle-${vehicleId}-trail`;
  const layerId = `vehicle-${vehicleId}-trail-layer`;

  const markerRef = useRef<maplibregl.Marker | null>(null);
  const iconPathRef = useRef<SVGPathElement | null>(null);
  const ringRef = useRef<HTMLDivElement | null>(null);
  const goldRingRef = useRef<HTMLDivElement | null>(null);
  const readyRef = useRef(false);
  const trailRef = useRef<[number, number][]>([]);
  const lastFollowPosRef = useRef<[number, number] | null>(null);
  const addedToMapRef = useRef(false);
  
  // Track last known lat for zoom calculations
  const lastLatRef = useRef<number | null>(null);

  // --- Setup / teardown of marker + trail source+layer, keyed on the map instance. ---
  useEffect(() => {
    if (!map) return;

    let cancelled = false;

    const setup = () => {
      if (cancelled) return;

      const { el, path, ringEl, goldRingEl } = createMarkerElement();
      iconPathRef.current = path;
      ringRef.current = ringEl;
      goldRingRef.current = goldRingEl;
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

    const onStyleData = () => {
      if (cancelled) return;
      // Re-add sources and layers if style changes wipe them out
      if (map.isStyleLoaded() && !map.getSource(sourceId)) {
        setup();
      }
    };
    map.on("styledata", onStyleData);

    const updateRingSize = () => {
      const ringEl = ringRef.current;
      const lat = lastLatRef.current;
      if (!ringEl || lat === null) return;
      
      const zoom = map.getZoom();
      // Calculate meters per pixel at current latitude
      const metersPerPixel = (156543.03392 * Math.cos((lat * Math.PI) / 180)) / Math.pow(2, zoom);
      
      // 1 km radius = 2000 meter diameter
      const diameterPixels = 2000 / metersPerPixel;
      ringEl.style.width = `${diameterPixels}px`;
      ringEl.style.height = `${diameterPixels}px`;
      
      const goldRingEl = goldRingRef.current;
      if (goldRingEl) {
        // 40m radius = 80 meter diameter
        const goldDiameterPixels = 80 / metersPerPixel;
        goldRingEl.style.width = `${goldDiameterPixels}px`;
        goldRingEl.style.height = `${goldDiameterPixels}px`;
      }
    };

    map.on("zoom", updateRingSize);

    return () => {
      cancelled = true;
      readyRef.current = false;
      map.off("idle", trySetup);
      map.off("styledata", onStyleData);
      map.off("zoom", updateRingSize);

      markerRef.current?.remove();
      markerRef.current = null;
      iconPathRef.current = null;
      ringRef.current = null;
      goldRingRef.current = null;
      addedToMapRef.current = false;

      if (map.getLayer(layerId)) map.removeLayer(layerId);
      if (map.getSource(sourceId)) map.removeSource(sourceId);

      trailRef.current = [];
      lastFollowPosRef.current = null;
      lastLatRef.current = null;
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
    lastLatRef.current = lat;

    marker.getElement().style.display = "";
    marker.setLngLat(pos);
    marker.setRotation(vehicle.attitude.yaw ?? 0);
    marker.getElement().style.opacity = vehicle.connected ? "1" : "0.4";

    const ringEl = ringRef.current;
    const goldRingEl = goldRingRef.current;
    if (ringEl && goldRingEl) {
      if (markerStyle === "high-vis") {
        ringEl.style.display = "block";
        goldRingEl.style.display = "block";
        const zoom = map.getZoom();
        const metersPerPixel = (156543.03392 * Math.cos((lat * Math.PI) / 180)) / Math.pow(2, zoom);
        // 1 km radius = 2000 meter diameter
        const diameterPixels = 2000 / metersPerPixel;
        ringEl.style.width = `${diameterPixels}px`;
        ringEl.style.height = `${diameterPixels}px`;
        // 40m radius = 80 meter diameter
        const goldDiameterPixels = 80 / metersPerPixel;
        goldRingEl.style.width = `${goldDiameterPixels}px`;
        goldRingEl.style.height = `${goldDiameterPixels}px`;
      } else {
        ringEl.style.display = "none";
        goldRingEl.style.display = "none";
      }
    }

    const path = iconPathRef.current;
    if (path) {
      if (markerStyle === "high-vis") {
        path.setAttribute("fill", "#00ff00"); // Green
        path.setAttribute("stroke", "#ffd700"); // Gold border
        path.setAttribute("stroke-width", "2.0");
        const svg = marker.getElement().querySelector("svg");
        if (svg) {
          svg.style.transform = "scale(1.0)";
          svg.style.transition = "transform 0.2s ease";
        }
      } else {
        path.setAttribute("fill", vehicle.armed ? ARMED_COLOR : DISARMED_COLOR);
        path.setAttribute("stroke", "rgba(10,18,22,0.85)");
        path.setAttribute("stroke-width", "1.5");
        const svg = marker.getElement().querySelector("svg");
        if (svg) {
          svg.style.transform = "scale(1)";
        }
      }
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
  }, [map, vehicle, follow, markerStyle, sourceId, layerId]);

  return null;
}
