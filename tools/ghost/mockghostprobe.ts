/**
 * MockLink ghost e2e probe (Wave 6 edition).
 *
 * Drives the real C++ ghost (QGC_ENABLE_QML=OFF, --headless --mock-link)
 * through the PROTOCOL.md handshake and the flagship arm/telemetry/mission
 * flow. Used by CI (see .github/workflows/ghost-strangler.yml) to guard the
 * strangler invariant that the MockLink bridge e2e stays green, and by
 * developers running against a locally booted ghost.
 *
 * Connects to the real ghost bridge, drives the PROTOCOL.md handshake, then:
 *  1. waits for tick with vehicleIds containing 128 (MockLink PX4)
 *  2. subscribes telemetry + mission for vehicle 128
 *  3. expects telemetry snapshots flowing (10 Hz) and a mission snapshot
 *  4. sends arm command → expects commandAck accepted (mavResult 0)
 *  5. expects telemetry armed:true
 *  6. sends missionDownload → expects missionItems response
 * Exit 0 on full pass, 1 on timeout/failure.
 *
 * Usage: PROBE_URL=ws://127.0.0.1:8899 bun tools/ghost/mockghostprobe.ts
 * (defaults to ws://127.0.0.1:8899 if PROBE_URL is unset). The caller is
 * responsible for waiting until the ghost's bridge port is accepting
 * connections before invoking this script — it does not retry a failed
 * initial connection, it only waits out DEADLINE_MS once open.
 */
const URL_WS = process.env.PROBE_URL ?? "ws://127.0.0.1:8899";
const VEHICLE = 128;
const DEADLINE_MS = 30_000;

const results: Record<string, boolean> = {
  tickVehicle: false,
  telemetryFlowing: false,
  missionSnapshot: false,
  armAccepted: false,
  armedTrue: false,
  missionDownload: false,
};
let telemetryCount = 0;

const ws = new WebSocket(URL_WS);
const send = (o: unknown) => ws.send(JSON.stringify(o));
let msgSeq = 0;
const nid = (p: string) => `${p}-${++msgSeq}`;

const timer = setTimeout(() => {
  console.error("TIMEOUT. results:", JSON.stringify(results), "telemetryCount:", telemetryCount);
  process.exit(1);
}, DEADLINE_MS);

let subscribed = false;
let armSent = false;
let downloadSent = false;

function maybeFinish() {
  if (Object.values(results).every(Boolean)) {
    clearTimeout(timer);
    console.log("PASS", JSON.stringify(results), "telemetryCount:", telemetryCount);
    ws.close();
    process.exit(0);
  }
}

ws.addEventListener("open", () => {
  console.log("connected", URL_WS);
  send({ type: "hello", id: nid("hello"), token: "dev", protocolVersion: "0.1" });
});

ws.addEventListener("message", (ev) => {
  if (typeof ev.data !== "string") return; // binary video frames — ignore
  const m = JSON.parse(ev.data);

  if (m.type === "tick" && Array.isArray(m.vehicleIds) && m.vehicleIds.includes(VEHICLE)) {
    results.tickVehicle = true;
    if (!subscribed) {
      subscribed = true;
      send({ type: "subscribe", id: nid("sub-tel"), channel: "telemetry", vehicleId: VEHICLE });
      send({ type: "subscribe", id: nid("sub-mis"), channel: "mission", vehicleId: VEHICLE });
    }
  }

  if (m.channel === "telemetry" && m.vehicleId === VEHICLE) {
    telemetryCount++;
    if (telemetryCount >= 5) results.telemetryFlowing = true;
    if (results.telemetryFlowing && !armSent) {
      armSent = true;
      send({ type: "command", id: "arm-1", vehicleId: VEHICLE, action: "arm", params: {} });
    }
    if (m.armed === true) results.armedTrue = true;
  }

  if (m.channel === "mission" && m.vehicleId === VEHICLE) {
    results.missionSnapshot = true;
    if (!downloadSent) {
      downloadSent = true;
      send({ type: "missionDownload", id: "dl-1", vehicleId: VEHICLE });
    }
  }

  if (m.type === "commandAck" && m.id === "arm-1") {
    console.log("commandAck:", JSON.stringify(m));
    if (m.status === "accepted") results.armAccepted = true;
    else { console.error("arm REJECTED"); process.exit(1); }
  }

  if (m.type === "missionItems" && m.id === "dl-1") {
    console.log("missionItems: count =", m.items?.length);
    results.missionDownload = true;
  }

  maybeFinish();
});

ws.addEventListener("error", (e) => { console.error("ws error", e); });
ws.addEventListener("close", () => { console.error("closed. results:", JSON.stringify(results)); });
