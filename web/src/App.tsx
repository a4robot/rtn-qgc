import { useEffect, useMemo, useState } from "react";
import type maplibregl from "maplibre-gl";

import { BridgeClient } from "./bridge/BridgeClient.ts";
import { BridgeContext } from "./bridge/BridgeContext.ts";
import { startBridgeSession } from "./bridge/session.ts";
import { ActionsPanel } from "./components/actions/ActionsPanel.tsx";
import { CoreMap } from "./components/map/CoreMap.tsx";
import { GotoOnClick } from "./components/map/GotoOnClick.tsx";
import { VehicleLayer } from "./components/map/VehicleLayer.tsx";
import { Attitude } from "./components/telemetry/Attitude.tsx";
import { Status } from "./components/telemetry/Status.tsx";
import { ParamTable } from "./components/params/ParamTable.tsx";
import { DualCam } from "./components/video/DualCam.tsx";
import { bindBridgeToStores, useConnection } from "./store/index.ts";

/**
 * Dev overrides: `?bridge=ws://127.0.0.1:8899` retargets the bridge (e.g. the
 * real ghost instead of the mock) and `?vehicle=128` picks the vehicle id
 * (MockLink and real autopilots use their own system ids). Defaults match the
 * mock. Multi-vehicle selection from the tick is a later job.
 */
const query = new URLSearchParams(window.location.search);
const BRIDGE_URL = query.get("bridge") ?? undefined;
const VEHICLE_ID = Number(query.get("vehicle") ?? 1);

export function App() {
  const connectionState = useConnection((s) => s.state);
  const [map, setMap] = useState<maplibregl.Map | null>(null);

  const client = useMemo(() => new BridgeClient(), []);

  useEffect(() => {
    const unbindStores = bindBridgeToStores(client);
    const stopSession = startBridgeSession(client, {
      url: BRIDGE_URL,
      vehicleId: VEHICLE_ID,
      videoStreamIds: [1, 2],
    });
    return () => {
      stopSession();
      unbindStores();
    };
  }, [client]);

  return (
    <BridgeContext.Provider value={client}>
      <div className="gcs-shell">
      <header className="gcs-header">
        <h1>RTN Ghost GCS</h1>
        <span className={`gcs-header-status gcs-header-status--${connectionState}`}>
          {connectionState}
        </span>
      </header>
      <main className="gcs-grid">
        <section className="gcs-panel gcs-video" aria-label="Video">
          <DualCam client={client} streamIds={[1, 2]} />
        </section>
        <section className="gcs-panel gcs-map" aria-label="Map">
          <CoreMap onMapReady={setMap} />
          <VehicleLayer map={map} vehicleId={VEHICLE_ID} follow />
          <GotoOnClick map={map} client={client} vehicleId={VEHICLE_ID} />
        </section>
        <section className="gcs-panel gcs-telemetry" aria-label="Telemetry">
          <div className="gcs-telemetry-row">
            <Attitude vehicleId={VEHICLE_ID} />
            <Status vehicleId={VEHICLE_ID} />
          </div>
        </section>
        <section className="gcs-panel gcs-actions" aria-label="Actions">
          <div className="gcs-actions-col">
            <ActionsPanel client={client} vehicleId={VEHICLE_ID} />
            <ParamTable vehicleId={VEHICLE_ID} />
          </div>
        </section>
      </main>
    </div>
    </BridgeContext.Provider>
  );
}
