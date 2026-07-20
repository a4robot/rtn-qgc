import React, { useEffect, useRef, useState } from "react";
import { useTranslation } from "react-i18next";
import maplibregl from "maplibre-gl";
import type { Map as MapLibreMap, StyleSpecification } from "maplibre-gl";
import "maplibre-gl/dist/maplibre-gl.css";
import "./map.css";

/** Default view: Sattahip Bay, Gulf of Thailand. */
const DEFAULT_CENTER: [number, number] = [100.9034, 12.6634];
const DEFAULT_ZOOM = 16;

import { useUiStore } from "../../store/uiStore.ts";
import { CelestialSkybox } from "./CelestialSkybox.tsx";

const CARTO_DARK_STYLE = "https://basemaps.cartocdn.com/gl/dark-matter-gl-style/style.json";
const ESRI_SATELLITE_STYLE: StyleSpecification = {
  version: 8,
  sources: {
    "esri-satellite": {
      type: "raster",
      tiles: ["https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"],
      tileSize: 256,
      attribution: "Tiles &copy; Esri"
    }
  },
  layers: [
    {
      id: "satellite",
      type: "raster",
      source: "esri-satellite",
      minzoom: 0,
      maxzoom: 22
    }
  ]
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
  const [mapInstance, setMapInstance] = useState<MapLibreMap | null>(null);
  const skipFirstMoveRef = useRef(true);

  const onMapReadyRef = useRef(onMapReady);
  const mapStyleType = useUiStore((state) => state.mapStyle);
  onMapReadyRef.current = onMapReady;

  const initialViewRef = useRef({ center, zoom });

  const { i18n } = useTranslation();
  const isThai = i18n.language?.startsWith('th');

  useEffect(() => {
    const container = containerRef.current;
    if (!container) return;

    const map = new maplibregl.Map({
      container,
      style: mapStyleType === "satellite" ? ESRI_SATELLITE_STYLE : CARTO_DARK_STYLE,
      center: initialViewRef.current.center,
      zoom: initialViewRef.current.zoom,
      attributionControl: false,
      pitchWithRotate: true,
      maxPitch: 85,
    });
    map.addControl(new maplibregl.NavigationControl({ visualizePitch: true }), "top-right");
    map.addControl(new maplibregl.ScaleControl({ unit: "metric" }), "bottom-left");
    mapRef.current = map;
    setMapInstance(map);

    map.once("load", () => {
      // 1. Change water color to the exact shade calculated from the styler
      if (mapStyleType !== "satellite") {
        if (map.getLayer("water")) {
          map.setPaintProperty("water", "fill-color", "#c6d4ec");
        }
        if (map.getLayer("waterway")) {
          map.setPaintProperty("waterway", "line-color", "#c6d4ec");
        }
        
        // Also set the background color to the exact shade calculated for land
        if (map.getLayer("background")) {
          map.setPaintProperty("background", "background-color", "#151919");
        }

        // 2. Hide POI and Transit
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
      }

      onMapReadyRef.current?.(map);
    });

    const observer = new ResizeObserver(() => {
      map.resize();
    });
    observer.observe(container);

    // Support Ctrl+Scroll for pitch adjustment
    const handleWheel = (e: WheelEvent) => {
      if (e.ctrlKey && !e.metaKey && !e.shiftKey && !e.altKey) {
        e.preventDefault();
        e.stopPropagation();
        const currentPitch = map.getPitch();
        const newPitch = Math.max(0, Math.min(85, currentPitch + (e.deltaY > 0 ? -5 : 5)));
        map.setPitch(newPitch);
      }
    };
    container.addEventListener("wheel", handleWheel, { passive: false, capture: true });

    return () => {
      container.removeEventListener("wheel", handleWheel, { capture: true });
      observer.disconnect();
      mapRef.current = null;
      setMapInstance(null);
      map.remove();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Sync map style when user changes it from settings
  useEffect(() => {
    const map = mapRef.current;
    if (map) {
      map.setStyle(mapStyleType === "satellite" ? ESRI_SATELLITE_STYLE : CARTO_DARK_STYLE);
    }
  }, [mapStyleType]);

  // Follow center/zoom prop changes without recreating the map.
  const [lon, lat] = center;
  useEffect(() => {
    if (skipFirstMoveRef.current) {
      skipFirstMoveRef.current = false;
      return;
    }
    mapRef.current?.easeTo({ center: [lon, lat], zoom });
  }, [lon, lat, zoom]);

  // Update map language when UI language changes
  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;
    
    const updateLang = () => {
      const style = map.getStyle();
      if (style && style.layers) {
        style.layers.forEach((layer) => {
          if (layer.type === "symbol") {
            const textField = map.getLayoutProperty(layer.id, "text-field");
            if (textField) {
              const strField = JSON.stringify(textField);
              if (strField.includes("{name}") || strField.includes("{name_en}")) {
                map.setLayoutProperty(layer.id, "text-field", isThai ? "{name}" : "{name_en}");
              }
            }
          }
        });
      }
    };

    if (map.isStyleLoaded()) {
      updateLang();
    } else {
      map.once("styledata", updateLang);
    }
  }, [isThai]);

  return (
    <>
      <CelestialSkybox map={mapInstance} />
      <div ref={containerRef} className="core-map" style={{ backgroundColor: "transparent" }} />
    </>
  );
}
