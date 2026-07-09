import { useEffect, useMemo, useRef, useState } from "react";
import type maplibregl from "maplibre-gl";

import { BridgeClient } from "./bridge/BridgeClient.ts";
import { BridgeContext } from "./bridge/BridgeContext.ts";
import { startBridgeSession, type BridgeSessionHandle } from "./bridge/session.ts";
import { ActionsPanel } from "./components/actions/ActionsPanel.tsx";
import { Instruments } from "./components/flyview/Instruments.tsx";
import { CoreMap } from "./components/map/CoreMap.tsx";
import { GotoOnClick } from "./components/map/GotoOnClick.tsx";
import { MissionLayer } from "./components/map/MissionLayer.tsx";
import { VehicleLayer } from "./components/map/VehicleLayer.tsx";
import { NotificationBell } from "./components/notifications/NotificationBell.tsx";
import { NotificationToasts } from "./components/notifications/NotificationToasts.tsx";
import { WaypointAdder } from "./components/plan/WaypointAdder.tsx";
import { WaypointList } from "./components/plan/WaypointList.tsx";
import { Attitude } from "./components/telemetry/Attitude.tsx";
import { Status } from "./components/telemetry/Status.tsx";
import { ParamTable } from "./components/params/ParamTable.tsx";
import { SidePanel } from "./components/SidePanel.tsx";
import { VehicleSelect } from "./components/VehicleSelect.tsx";
import { DualCam } from "./components/video/DualCam.tsx";
import {
  bindBridgeToStores,
  useActiveVehicle,
  useConnection,
  useMapMode,
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

export function App() {
  const connectionState = useConnection((s) => s.state);
  const [map, setMap] = useState<maplibregl.Map | null>(null);
  const activeVehicleId = useActiveVehicle() ?? INITIAL_VEHICLE_ID;
  const setActiveVehicleId = useUiStore((s) => s.setActiveVehicleId);
  const mapMode = useMapMode();
  const sessionRef = useRef<BridgeSessionHandle | null>(null);

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
      <header className="gcs-header">
        <h1>RTN Ghost GCS</h1>
        <VehicleSelect activeVehicleId={activeVehicleId} onSelect={onSelectVehicle} />
        <div className="gcs-header-right">
          <NotificationBell />
          <span className={`gcs-header-status gcs-header-status--${connectionState}`}>
            {connectionState}
          </span>
        </div>
      </header>
      <NotificationToasts />
      <main className="gcs-grid">
        <section className="gcs-panel gcs-video" aria-label="Video">
          <DualCam client={client} streamIds={[1, 2]} />
        </section>
        <section className="gcs-panel gcs-map" aria-label="Map">
          <CoreMap onMapReady={setMap} />
          <VehicleLayer map={map} vehicleId={activeVehicleId} follow />
          <MissionLayer map={map} vehicleId={activeVehicleId} />
          {/* Fly-mode clicks are goto; plan-mode clicks add draft waypoints.
              WaypointAdder gates its own clicks, GotoOnClick is mode-blind —
              mount it only in fly mode so the two never race one click. */}
          {mapMode === "fly" && (
            <GotoOnClick map={map} client={client} vehicleId={activeVehicleId} />
          )}
          <WaypointAdder map={map} client={client} vehicleId={activeVehicleId} />
        </section>
        <section className="gcs-panel gcs-telemetry" aria-label="Telemetry">
          <Instruments vehicleId={activeVehicleId} />
          <div className="gcs-telemetry-row">
            <Attitude vehicleId={activeVehicleId} />
            <Status vehicleId={activeVehicleId} />
          </div>
        </section>
        <section className="gcs-panel gcs-actions" aria-label="Actions">
          <SidePanel
            flyContent={<ActionsPanel client={client} vehicleId={activeVehicleId} />}
            planContent={<WaypointList client={client} vehicleId={activeVehicleId} />}
            paramsContent={<ParamTable vehicleId={activeVehicleId} />}
          />
        </section>
      </main>
    </div>
    </BridgeContext.Provider>
  );
}
