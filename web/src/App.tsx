export function App() {
  return (
    <div className="gcs-shell">
      <header className="gcs-header">
        <h1>RTN Ghost GCS</h1>
        <span className="gcs-header-status">disconnected</span>
      </header>
      <main className="gcs-grid">
        <section className="gcs-panel gcs-video" aria-label="Video">
          <span className="gcs-panel-label">Video</span>
        </section>
        <section className="gcs-panel gcs-map" aria-label="Map">
          <span className="gcs-panel-label">Map</span>
        </section>
        <section className="gcs-panel gcs-telemetry" aria-label="Telemetry">
          <span className="gcs-panel-label">Telemetry</span>
        </section>
        <section className="gcs-panel gcs-actions" aria-label="Actions">
          <span className="gcs-panel-label">Actions</span>
        </section>
      </main>
    </div>
  );
}
