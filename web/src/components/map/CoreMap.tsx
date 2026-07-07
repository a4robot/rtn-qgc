import { useEffect, useRef } from "react";
import maplibregl from "maplibre-gl";
import type { Map as MapLibreMap, StyleSpecification } from "maplibre-gl";
import "maplibre-gl/dist/maplibre-gl.css";
import "./map.css";

/** Default view: Sattahip Bay, Gulf of Thailand. */
const DEFAULT_CENTER: [number, number] = [100.9034, 12.6634];
const DEFAULT_ZOOM = 16;

/**
 * Key-free raster style: OSM tiles over a dark background, with the raster
 * dimmed/desaturated slightly so it doesn't clash with the dark UI shell.
 */
const OSM_DARK_STYLE: StyleSpecification = {
  version: 8,
  sources: {
    osm: {
      type: "raster",
      tiles: ["https://tile.openstreetmap.org/{z}/{x}/{y}.png"],
      tileSize: 256,
      maxzoom: 19,
      attribution:
        '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
    },
  },
  layers: [
    {
      id: "background",
      type: "background",
      paint: { "background-color": "#0d1117" },
    },
    {
      id: "osm",
      type: "raster",
      source: "osm",
      paint: {
        "raster-brightness-max": 0.8,
        "raster-saturation": -0.3,
      },
    },
  ],
};

export interface CoreMapProps {
  /** [longitude, latitude] */
  center?: [number, number];
  zoom?: number;
  /** Called once the map has loaded; hook point for markers, click handlers, etc. */
  onMapReady?: (map: MapLibreMap) => void;
}

export function CoreMap({
  center = DEFAULT_CENTER,
  zoom = DEFAULT_ZOOM,
  onMapReady,
}: CoreMapProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const mapRef = useRef<MapLibreMap | null>(null);
  const skipFirstMoveRef = useRef(true);

  const onMapReadyRef = useRef(onMapReady);
  onMapReadyRef.current = onMapReady;

  const initialViewRef = useRef({ center, zoom });

  useEffect(() => {
    const container = containerRef.current;
    if (!container) return;

    const map = new maplibregl.Map({
      container,
      style: OSM_DARK_STYLE,
      center: initialViewRef.current.center,
      zoom: initialViewRef.current.zoom,
    });
    map.addControl(new maplibregl.NavigationControl(), "top-right");
    map.addControl(new maplibregl.ScaleControl({ unit: "metric" }), "bottom-left");
    mapRef.current = map;

    map.once("load", () => {
      onMapReadyRef.current?.(map);
    });

    const observer = new ResizeObserver(() => {
      map.resize();
    });
    observer.observe(container);

    return () => {
      observer.disconnect();
      mapRef.current = null;
      map.remove();
    };
  }, []);

  // Follow center/zoom prop changes without recreating the map.
  const [lon, lat] = center;
  useEffect(() => {
    if (skipFirstMoveRef.current) {
      skipFirstMoveRef.current = false;
      return;
    }
    mapRef.current?.easeTo({ center: [lon, lat], zoom });
  }, [lon, lat, zoom]);

  return <div ref={containerRef} className="core-map" />;
}
