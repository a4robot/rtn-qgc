import { useEffect, useMemo, useState } from "react";
import type maplibregl from "maplibre-gl";

import { BridgeClient } from "./bridge/BridgeClient.ts";
import { startBridgeSession } from "./bridge/session.ts";
import { CoreMap } from "./components/map/CoreMap.tsx";
import { VehicleLayer } from "./components/map/VehicleLayer.tsx";
import { Attitude } from "./components/telemetry/Attitude.tsx";
import { Status } from "./components/telemetry/Status.tsx";
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
    const stopSession = startBridgeSession(client, { vehicleId: VEHICLE_ID });
    return () => {
      stopSession();
      unbindStores();
    };
  }, [client]);

  return (
    <div className="gcs-shell">
      <header className="gcs-header">
        <h1>RTN Ghost GCS</h1>
        <span className={`gcs-header-status gcs-header-status--${connectionState}`}>
          {connectionState}
        </span>
      </header>
      <main className="gcs-grid">
        <section className="gcs-panel gcs-video" aria-label="Video">
          <VideoPlayer streamId={1} />
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
          <span className="gcs-panel-label">Actions</span>
        </section>
      </main>
    </div>
  );
}
