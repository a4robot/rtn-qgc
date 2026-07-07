import { useEffect, useMemo, useState } from "react";
import type maplibregl from "maplibre-gl";

import { BridgeClient } from "./bridge/BridgeClient.ts";
import { BridgeContext } from "./bridge/BridgeContext.ts";
import { startBridgeSession } from "./bridge/session.ts";
import { ActionsPanel } from "./components/actions/ActionsPanel.tsx";
import { CoreMap } from "./components/map/CoreMap.tsx";
import { VehicleLayer } from "./components/map/VehicleLayer.tsx";
import { Attitude } from "./components/telemetry/Attitude.tsx";
import { Status } from "./components/telemetry/Status.tsx";
import { ParamTable } from "./components/params/ParamTable.tsx";
import { VideoPlayer } from "./components/video/VideoPlayer.tsx";
import { bindBridgeToStores, useConnection } from "./store/index.ts";

/** Single vehicle for now; multi-vehicle selection is a later job. */
const VEHICLE_ID = 1;

export function App() {
  const connectionState = useConnection((s) => s.state);
  const [map, setMap] = useState<maplibregl.Map | null>(null);

  const client = useMemo(() => new BridgeClient(), []);

  useEffect(() => {
    const unbindStores = bindBridgeToStores(client);
    const stopSession = startBridgeSession(client, {
      vehicleId: VEHICLE_ID,
      videoStreamIds: [1],
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
          <VideoPlayer streamId={1} client={client} />
        </section>
        <section className="gcs-panel gcs-map" aria-label="Map">
          <CoreMap onMapReady={setMap} />
          <VehicleLayer map={map} vehicleId={VEHICLE_ID} follow />
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
