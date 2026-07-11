import { useEffect, useRef } from "react";
import maplibregl from "maplibre-gl";
import type {
  IControl,
  Map as MapLibreMap,
  SkySpecification,
} from "maplibre-gl";
import "maplibre-gl/dist/maplibre-gl.css";
import "./map.css";

/** Default view: Sattahip Bay, Gulf of Thailand. */
const DEFAULT_CENTER: [number, number] = [100.9034, 12.6634];
const DEFAULT_ZOOM = 16;

const CARTO_DARK_STYLE = "https://basemaps.cartocdn.com/gl/dark-matter-gl-style/style.json";

/**
 * Free, key-free elevation source (terrarium-encoded PNGs, per-pixel RGB
 * elevation) — same tileset Mapzen originally published, now mirrored by
 * AWS's Open Data program. Network-only: no local fallback, so a blocked or
 * offline host must degrade to flat rather than break the map (see the
 * "error" listener in the init effect).
 */
const TERRAIN_SOURCE_ID = "terrain-dem";
const TERRAIN_TILE_URL =
  "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png";
const TERRAIN_ATTRIBUTION =
  '&copy; <a href="https://github.com/tilezen/joerd/blob/master/docs/attribution.md">Mapzen Terrain Tiles</a>';
/** Vertical exaggeration applied to the DEM — enough to read as terrain without caricaturing it. */
const TERRAIN_EXAGGERATION = 1.2;

const PITCH_3D = 55;
const PITCH_2D = 0;

/**
 * Extruded building blocks (Google-Earth-ish massing) from the Carto
 * basemap's own `building` source-layer (openmaptiles schema:
 * `render_height`/`render_min_height` metres). Hidden in 2D — flat maps
 * with extrusion artifacts just look broken.
 */
const BUILDINGS_3D_LAYER_ID = "rtn-buildings-3d";

/**
 * Below this canvas width/height (px), maplibre's terrain render pipeline
 * renders solid black in this app's target environment (observed on the
 * ~320x180 video-PiP swap slot, `.stage-slot--pip` in stage.css — reproduces
 * with terrain on, disappears with terrain off at the same size, so it's the
 * terrain render path specifically, not the PiP swap itself). The DOM
 * vehicle marker and every other layer are unaffected — only the elevation
 * drape breaks. Guarded defensively: terrain is dropped (flat, pitch kept)
 * below this size and restored once the container grows back past it.
 */
const TERRAIN_MIN_DIMENSION_PX = 400;

/** Subtle dark-theme sky + fog, only visually apparent once pitched into 3D. */
const SKY_CONFIG: SkySpecification = {
  "sky-color": "#0d1117",
  "horizon-color": "#3b4656",
  "fog-color": "#0d1117",
  "fog-ground-blend": 0.6,
  "horizon-fog-blend": 0.8,
  "sky-horizon-blend": 0.6,
  "atmosphere-blend": 0.6,
};

const MODE3D_STORAGE_KEY = "rtn-gcs.map.mode3d";

/** Read the persisted 2D/3D preference. Defaults to false (2D) if unset or unreadable. */
function loadMode3D(): boolean {
  if (typeof window === "undefined" || !window.localStorage) {
    return false;
  }
  try {
    return window.localStorage.getItem(MODE3D_STORAGE_KEY) === "1";
  } catch {
    return false;
  }
}

/** Persist the 2D/3D preference. Silently no-ops if storage is unavailable. */
function saveMode3D(active: boolean): void {
  if (typeof window === "undefined" || !window.localStorage) {
    return;
  }
  try {
    window.localStorage.setItem(MODE3D_STORAGE_KEY, active ? "1" : "0");
  } catch {
    // private mode / quota exceeded — preference just won't persist.
  }
}

interface Toggle3DControl extends IControl {
  setActive(active: boolean): void;
}

/**
 * Small maplibre control (own button, grouped like NavigationControl) that
 * toggles between flat 2D and pitched 3D-terrain view. Visual state only —
 * all the actual pitch/terrain mechanics live in the caller's `onToggle`.
 */
function createToggle3DControl(onToggle: () => void): Toggle3DControl {
  let container: HTMLDivElement;
  let button: HTMLButtonElement;

  return {
    onAdd() {
      container = document.createElement("div");
      container.className = "maplibregl-ctrl maplibregl-ctrl-group core-map-3d-ctrl";

      button = document.createElement("button");
      button.type = "button";
      button.className = "core-map-3d-btn";
      button.textContent = "2D";
      button.setAttribute("aria-label", "Toggle 3D terrain view");
      button.addEventListener("click", onToggle);
      container.appendChild(button);

      return container;
    },
    onRemove() {
      button.removeEventListener("click", onToggle);
      container.parentNode?.removeChild(container);
    },
    setActive(active: boolean) {
      button.textContent = active ? "3D" : "2D";
      button.title = active ? "Switch to flat 2D view" : "Switch to tilted 3D terrain view";
      button.classList.toggle("core-map-3d-btn--active", active);
    },
  };
}

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
      // dragRotate/pitchWithRotate default to true — right-drag (or
      // ctrl-drag) pitch works out of the box, left as maplibre defaults.
    });
    // visualizePitch tilts the compass icon with the camera — cheap visual
    // confirmation that the map is in 3D beyond the toggle button itself.
    // bottom-right, not top-right: the instrument column floats over the
    // map's top-right corner (see stage.css `.instrument-column-float`) and
    // the RTN quick-action rail owns the top-left — the attitude indicator
    // was literally sitting on the zoom/compass buttons. Bottom-right only
    // shares with the attribution line, which stacks below controls.
    map.addControl(new maplibregl.NavigationControl({ visualizePitch: true }), "bottom-right");
    map.addControl(new maplibregl.ScaleControl({ unit: "metric" }), "bottom-left");
    mapRef.current = map;

    let mode3D = loadMode3D();
    // Becomes true once the DEM source is registered without throwing;
    // flipped back off for the rest of the session on the first tile error
    // (offline / blocked host) so we don't keep retrying a dead source.
    let terrainAvailable = false;
    let terrainDegraded = false;

    const canvasTooSmallForTerrain = () =>
      container.clientWidth < TERRAIN_MIN_DIMENSION_PX ||
      container.clientHeight < TERRAIN_MIN_DIMENSION_PX;

    // Re-derives the terrain on/off state from current mode/availability/size
    // — called on toggle and again on every resize (see the ResizeObserver
    // below), since the PiP swap can cross the small-canvas threshold in
    // either direction without the user touching the 2D/3D toggle at all.
    const syncTerrain = () => {
      const wantTerrain =
        mode3D && terrainAvailable && !terrainDegraded && !canvasTooSmallForTerrain();
      map.setTerrain(
        wantTerrain ? { source: TERRAIN_SOURCE_ID, exaggeration: TERRAIN_EXAGGERATION } : null,
      );
    };

    const applyMode3D = (active: boolean, opts: { animate: boolean }) => {
      mode3D = active;
      saveMode3D(active);
      toggleControl.setActive(active);

      syncTerrain();

      // Google-Earth-style block buildings, only worth drawing pitched.
      if (map.getLayer(BUILDINGS_3D_LAYER_ID)) {
        map.setLayoutProperty(BUILDINGS_3D_LAYER_ID, "visibility", active ? "visible" : "none");
      }

      const pitch = active ? PITCH_3D : PITCH_2D;
      if (opts.animate) {
        map.easeTo({ pitch, duration: 600 });
      } else {
        map.jumpTo({ pitch });
      }
    };

    const toggleControl = createToggle3DControl(() => applyMode3D(!mode3D, { animate: true }));
    map.addControl(toggleControl, "bottom-right");

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

      if (!map.getLayer(BUILDINGS_3D_LAYER_ID)) {
        map.addLayer({
          id: BUILDINGS_3D_LAYER_ID,
          type: "fill-extrusion",
          source: "carto",
          "source-layer": "building",
          minzoom: 13,
          layout: { visibility: "none" },
          paint: {
            "fill-extrusion-color": "#1d2a2a",
            "fill-extrusion-height": ["coalesce", ["get", "render_height"], 12],
            "fill-extrusion-base": ["coalesce", ["get", "render_min_height"], 0],
            "fill-extrusion-opacity": 0.85,
          },
        });
      }

      try {
        map.addSource(TERRAIN_SOURCE_ID, {
          type: "raster-dem",
          tiles: [TERRAIN_TILE_URL],
          tileSize: 256,
          encoding: "terrarium",
          maxzoom: 15,
          attribution: TERRAIN_ATTRIBUTION,
        });
        terrainAvailable = true;
      } catch {
        // Source registration itself failed synchronously (malformed spec,
        // duplicate id) — treat exactly like a network failure: stay flat.
        terrainAvailable = false;
      }

      map.setSky(SKY_CONFIG);

      // Apply the persisted 2D/3D preference now that the style (and, best
      // effort, the terrain source) is in place. No animation on initial
      // load — only user-triggered toggles ease.
      applyMode3D(mode3D, { animate: false });

      onMapReadyRef.current?.(map);
    });

    // maplibre 4.x doesn't export its ErrorEvent type — the only field this
    // handler reads is the (undocumented) sourceId stamped on source errors.
    const onMapError = (event: { sourceId?: string }) => {
      if (event.sourceId !== TERRAIN_SOURCE_ID || terrainDegraded) return;
      // Tile fetch failed (offline / blocked host) — drop the elevation
      // drape silently. Pitch is left as-is; the map just goes flat under
      // the camera angle rather than erroring or looping retries.
      terrainDegraded = true;
      syncTerrain();
    };
    map.on("error", onMapError);

    const observer = new ResizeObserver(() => {
      map.resize();
      syncTerrain();
    });
    observer.observe(container);

    return () => {
      observer.disconnect();
      map.off("error", onMapError);
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
