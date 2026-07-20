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
import { RTNPanel } from "./components/flyview/RTNPanel.tsx";
import { RTNInstruments } from "./components/flyview/RTNInstruments.tsx";
import {
  bindBridgeToStores,
  useActiveVehicle,
  useMapMode,
  useSidePanelTab,
  useUiStore,
} from "./store/index.ts";

function PipResizeHandle({ position }: { position: "tr" | "br" }) {
  const handleMouseDown = (e: React.MouseEvent) => {
    e.stopPropagation();
    e.preventDefault();
    const startX = e.clientX;
    const startY = e.clientY;
    const stage = (e.target as HTMLElement).closest('.gcs-stage') as HTMLElement;
    const pip = (e.target as HTMLElement).closest('.stage-slot--pip') as HTMLElement;
    if (!stage || !pip) return;
    const startW = pip.offsetWidth;
    const startH = pip.offsetHeight;

    const onMouseMove = (moveEvent: MouseEvent) => {
      const dx = moveEvent.clientX - startX;
      const dy = moveEvent.clientY - startY;
      let newW = startW + dx;
      let newH = position === "tr" ? startH - dy : startH + dy;
      
      newW = Math.max(240, Math.min(newW, window.innerWidth * 0.8));
      newH = Math.max(135, Math.min(newH, window.innerHeight * 0.8));
      
      stage.style.setProperty('--pip-w', `${newW}px`);
      stage.style.setProperty('--pip-h', `${newH}px`);
    };

    const onMouseUp = () => {
      document.removeEventListener('mousemove', onMouseMove);
      document.removeEventListener('mouseup', onMouseUp);
    };

    document.addEventListener('mousemove', onMouseMove);
    document.addEventListener('mouseup', onMouseUp);
  };

  return (
    <div
      className="pip-resize-handle"
      onMouseDown={handleMouseDown}
      style={{
        position: "absolute",
        [position === "tr" ? "top" : "bottom"]: 0,
        right: 0,
        width: 32,
        height: 32,
        cursor: position === "tr" ? "ne-resize" : "se-resize",
        zIndex: 10,
        display: "flex",
        alignItems: position === "tr" ? "flex-start" : "flex-end",
        justifyContent: "flex-end",
        padding: "4px",
        color: "rgba(255,255,255,0.8)"
      }}
    >
      <svg width="16" height="16" viewBox="0 0 24 24" style={{ transform: position === "tr" ? "rotate(-90deg)" : "none", opacity: 0.5 }}>
        <path fill="currentColor" d="M11 22h11V11z" />
      </svg>
    </div>
  );
}

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

  const pipClickRef = useRef<{ x: number; y: number } | null>(null);
  
  const handlePipSwap = (e: React.MouseEvent, targetVideoFullscreen: boolean) => {
    // If we clicked on a resize handle, don't swap
    if ((e.target as HTMLElement).closest('.pip-resize-handle')) return;
    
    if (pipClickRef.current) {
      const dx = Math.abs(e.clientX - pipClickRef.current.x);
      const dy = Math.abs(e.clientY - pipClickRef.current.y);
      pipClickRef.current = null;
      if (dx > 5 || dy > 5) return; // Was a drag, ignore click
    }
    setVideoFullscreen(targetVideoFullscreen);
  };

  const client = useMemo(() => new BridgeClient(), []);

  useEffect(() => {
    const stage = document.querySelector('.gcs-stage') as HTMLElement;
    if (!stage) return;
    const observer = new ResizeObserver((entries) => {
      for (const entry of entries) {
        if (entry.target.classList.contains('stage-slot--pip')) {
          const height = (entry.target as HTMLElement).offsetHeight;
          stage.style.setProperty('--pip-actual-height', `${height}px`);
        }
      }
    });
    const slots = document.querySelectorAll('.stage-slot');
    slots.forEach(slot => observer.observe(slot));
    return () => observer.disconnect();
  }, []);

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
            onMouseDown={videoFullscreen ? (e) => pipClickRef.current = { x: e.clientX, y: e.clientY } : undefined}
            onClick={videoFullscreen ? (e) => handlePipSwap(e, false) : undefined}
          >
            {videoFullscreen && (
              <>
                <PipResizeHandle position="tr" />
                <PipResizeHandle position="br" />
              </>
            )}
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
            onMouseDown={!videoFullscreen ? (e) => pipClickRef.current = { x: e.clientX, y: e.clientY } : undefined}
            onClick={!videoFullscreen ? (e) => handlePipSwap(e, true) : undefined}
          >
            {!videoFullscreen && (
              <>
                <PipResizeHandle position="tr" />
                <PipResizeHandle position="br" />
              </>
            )}
            <VideoStage client={client} streamIds={[1, 2]} />
          </div>

          <div className="instrument-column-float">
            <InstrumentColumn vehicleId={activeVehicleId} map={map} />
          </div>

          <div className={`rtn-panel-float${planOpen ? " stage-shift-right" : ""}`}>
            <RTNPanel />
            <RTNInstruments />
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
