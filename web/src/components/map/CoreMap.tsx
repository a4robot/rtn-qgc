import { useEffect, useRef } from "react";
import maplibregl from "maplibre-gl";
import type { Map as MapLibreMap, StyleSpecification } from "maplibre-gl";
import "maplibre-gl/dist/maplibre-gl.css";
import "./map.css";

/** Default view: Sattahip Bay, Gulf of Thailand. */
const DEFAULT_CENTER: [number, number] = [100.9034, 12.6634];
const DEFAULT_ZOOM = 16;

const CARTO_DARK_STYLE = "https://basemaps.cartocdn.com/gl/dark-matter-gl-style/style.json";

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
      style: CARTO_DARK_STYLE,
      center: initialViewRef.current.center,
      zoom: initialViewRef.current.zoom,
    });
    map.addControl(new maplibregl.NavigationControl(), "top-right");
    map.addControl(new maplibregl.ScaleControl({ unit: "metric" }), "bottom-left");
    mapRef.current = map;

    map.once("load", () => {
      // Colors computed by running the requested Google styled-map rules
      // exactly (hue styler = replace H, keep S/L; saturation −50 = S×0.5;
      // invert_lightness = L→1−L; the "all" rule and the "water" rule BOTH
      // match water, so its lightness inverts twice and cancels):
      //   water: base #b3e1ff = hsl(203°,100%,85%) → hue(#005eff)=218°,
      //          S×0.5 → hsl(218°,50%,85%) = #c6d4ec  (matches the given
      //          calibration point bit-exactly)
      //   land:  base #f2efe9 = hsl(40°,25%,93%) → hue(#131c1c)=180°,
      //          S×0.5, L inverted → hsl(180°,13%,7%) = #0f1414
      if (map.getLayer("water")) {
        map.setPaintProperty("water", "fill-color", "#c6d4ec");
      }
      if (map.getLayer("waterway")) {
        map.setPaintProperty("waterway", "line-color", "#c6d4ec");
      }
      if (map.getLayer("background")) {
        map.setPaintProperty("background", "background-color", "#0f1414");
      }

      // 2. & 3. Hide POI and Transit
      const style = map.getStyle();
      if (style && style.layers) {
        style.layers.forEach((layer) => {
          if (
            layer.id.includes("poi") ||
            layer.id.includes("transit") ||
            layer.id.includes("railway") ||
            layer.id.includes("station")
          ) {
            map.setLayoutProperty(layer.id, "visibility", "none");
          }
        });
      }

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
