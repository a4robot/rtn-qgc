import { useEffect, useMemo, useRef, useState } from "react";
import type maplibregl from "maplibre-gl";

import { BridgeClient } from "./bridge/BridgeClient.ts";
import { BridgeContext } from "./bridge/BridgeContext.ts";
import { startBridgeSession, type BridgeSessionHandle } from "./bridge/session.ts";
import { ActionsPanel } from "./components/actions/ActionsPanel.tsx";
import { InstrumentColumn } from "./components/flyview/InstrumentColumn.tsx";
import { CoreMap } from "./components/map/CoreMap.tsx";
import { GotoOnClick } from "./components/map/GotoOnClick.tsx";
import { MissionLayer } from "./components/map/MissionLayer.tsx";
import { VehicleLayer } from "./components/map/VehicleLayer.tsx";
import { NotificationToasts } from "./components/notifications/NotificationToasts.tsx";
import { OverlayDrawer } from "./components/overlay/OverlayDrawer.tsx";
import { PlanDrawer } from "./components/plan/PlanDrawer.tsx";
import { WaypointAdder } from "./components/plan/WaypointAdder.tsx";
import "./components/stage.css";
import { ImageCard } from "./components/telemetry/ImageCard.tsx";
import { TopToolbar } from "./components/toolbar/TopToolbar.tsx";
import { VideoStage } from "./components/video/VideoStage.tsx";
import {
  bindBridgeToStores,
  useActiveVehicle,
  useMapMode,
  useSidePanelTab,
  useUiStore,
} from "./store/index.ts";

/**
 * Dev overrides: `?bridge=ws://127.0.0.1:8899` retargets the bridge (e.g. the
 * real ghost instead of the mock) and `?vehicle=128` seeds the initially
 * selected vehicle (MockLink and real autopilots use their own system ids).
 * After connect, the header selector switches vehicles live from the tick.
 */
const query = new URLSearchParams(window.location.search);
const BRIDGE_URL = query.get("bridge") ?? undefined;
const INITIAL_VEHICLE_ID = Number(query.get("vehicle") ?? 1);

/**
 * Cockpit layout — QGC FlyView placement: a full-viewport map behind
 * everything, a fixed top toolbar, video as a bottom-left PiP over the map
 * (click to swap fullscreen with the map — see `.stage-slot--full`/
 * `--pip` in stage.css), a floating right-edge instrument column, a
 * floating bottom-center guided-actions strip, and left/right drawers for
 * Plan and Params/Settings. See `useConnection`'s `state` for the toolbar's
 * connection chip.
 */
export function App() {
  const [map, setMap] = useState<maplibregl.Map | null>(null);
  const activeVehicleId = useActiveVehicle() ?? INITIAL_VEHICLE_ID;
  const setActiveVehicleId = useUiStore((s) => s.setActiveVehicleId);
  const mapMode = useMapMode();
  const tab = useSidePanelTab();
  const sessionRef = useRef<BridgeSessionHandle | null>(null);
  // Presentation-only: which slot (map or video) is currently fullscreen.
  // The map instance itself never remounts on swap — only this class swaps
  // (see stage.css) — so VehicleLayer/MissionLayer/etc. are unaffected.
  const [videoFullscreen, setVideoFullscreen] = useState(false);
  // The Plan drawer shares the left edge with the PiP/image-card — shift
  // both clear of it while it's open (see stage.css's `.stage-shift-right`).
  const planOpen = tab === "plan";

  const client = useMemo(() => new BridgeClient(), []);

  useEffect(() => {
    setActiveVehicleId(INITIAL_VEHICLE_ID);
    const unbindStores = bindBridgeToStores(client);
    const session = startBridgeSession(client, {
      url: BRIDGE_URL,
      initialVehicleId: INITIAL_VEHICLE_ID,
      videoStreamIds: [1, 2],
    });
    sessionRef.current = session;
    return () => {
      sessionRef.current = null;
      session.stop();
      unbindStores();
    };
  }, [client, setActiveVehicleId]);

  const onSelectVehicle = (id: number) => {
    setActiveVehicleId(id);
    sessionRef.current?.setVehicle(id);
  };

  return (
    <BridgeContext.Provider value={client}>
      <div className="gcs-shell">
        <TopToolbar activeVehicleId={activeVehicleId} onSelectVehicle={onSelectVehicle} />
        <NotificationToasts />

        <main className="gcs-stage" aria-label="Cockpit">
          <div
            className={`stage-slot ${videoFullscreen ? "stage-slot--pip" : "stage-slot--full"}${
              videoFullscreen && planOpen ? " stage-shift-right" : ""
            }`}
            aria-label="Map"
            onClick={videoFullscreen ? () => setVideoFullscreen(false) : undefined}
          >
            <CoreMap onMapReady={setMap} />
            <VehicleLayer map={map} vehicleId={activeVehicleId} follow />
            <MissionLayer map={map} vehicleId={activeVehicleId} />
            {/* Fly-mode clicks are goto; plan-mode clicks add draft waypoints.
                WaypointAdder gates its own clicks, GotoOnClick is mode-blind —
                mount it only in fly mode so the two never race one click.
                Neither mounts while the map is the PiP: QGC's PiP is a
                non-interactive preview, and a PiP click must mean only
                "swap views", never a goto/waypoint on a 320px map. */}
            {mapMode === "fly" && !videoFullscreen && (
              <GotoOnClick map={map} client={client} vehicleId={activeVehicleId} />
            )}
            {!videoFullscreen && (
              <WaypointAdder map={map} client={client} vehicleId={activeVehicleId} />
            )}
          </div>

          <div
            className={`stage-slot ${videoFullscreen ? "stage-slot--full" : "stage-slot--pip"}${
              !videoFullscreen && planOpen ? " stage-shift-right" : ""
            }`}
            aria-label="Video"
            onClick={!videoFullscreen ? () => setVideoFullscreen(true) : undefined}
          >
            <VideoStage client={client} streamIds={[1, 2]} />
          </div>

          <div className="instrument-column-float">
            <InstrumentColumn vehicleId={activeVehicleId} />
          </div>

          <div className={`image-card-float${planOpen ? " stage-shift-right" : ""}`}>
            <ImageCard vehicleId={activeVehicleId} />
          </div>

          <div className="guided-strip">
            <ActionsPanel client={client} vehicleId={activeVehicleId} />
          </div>

          <PlanDrawer client={client} vehicleId={activeVehicleId} />
          <OverlayDrawer client={client} vehicleId={activeVehicleId} />
        </main>
      </div>
    </BridgeContext.Provider>
  );
}
