/**
 * StateMachine transition-log capture probe (Wave 15 / M8 Q8a).
 *
 * Drives a MockLink ghost through the flows that exercise the three
 * QGCStateMachine-derived machines this wave is auditing before the
 * Q8b port off QStateMachine:
 *   - InitialConnectStateMachine   (fires automatically on vehicle connect)
 *   - ComponentInformationManager / RequestMetaDataTypeStateMachine
 *     (fired from within InitialConnectStateMachine's RequestCompInfo state)
 *   - ParameterManager's ad-hoc "PARAM_SET" QGCStateMachine
 *     (fired explicitly here via a setParam WebBridge command, since the
 *     bulk startup parameter download path does NOT use a QGCStateMachine
 *     - see AUDIT.md "ParameterManager" section for why)
 *
 * This script does not assert pass/fail on the state machine internals
 * (that's what the transition log capture is for) - it just drives the
 * flows deterministically enough for the C++ side to log a comparable
 * sequence, then exits. capture.sh greps the container's stdout for the
 * relevant logging categories and normalizes it into a baseline log.
 */
const URL_WS = process.env.PROBE_URL ?? "ws://127.0.0.1:8899";
const VEHICLE = 128;
const DEADLINE_MS = 40_000;
// MockLink PX4MockLink.params ships this - harmless to round-trip set.
const PARAM_NAME = "BAT1_CAPACITY";

const ws = new WebSocket(URL_WS);
const send = (o: unknown) => ws.send(JSON.stringify(o));
let msgSeq = 0;
const nid = (p: string) => `${p}-${++msgSeq}`;
const startMs = Date.now();

const timer = setTimeout(() => {
  console.error("TIMEOUT waiting for capture flow to complete");
  process.exit(1);
}, DEADLINE_MS);

let subscribed = false;
let paramSetSent = false;
let paramSetAcked = false;

ws.addEventListener("open", () => {
  console.log("connected", URL_WS);
  send({ type: "hello", id: nid("hello"), token: "dev", protocolVersion: "0.1" });
});

ws.addEventListener("message", (ev) => {
  if (typeof ev.data !== "string") return; // binary video frames - ignore
  const m = JSON.parse(ev.data);

  if (m.type === "tick" && Array.isArray(m.vehicleIds) && m.vehicleIds.includes(VEHICLE)) {
    if (!subscribed) {
      subscribed = true;
      send({ type: "subscribe", id: nid("sub-tel"), channel: "telemetry", vehicleId: VEHICLE });
    }
  }

  if (m.channel === "telemetry" && m.vehicleId === VEHICLE) {
    // Give InitialConnectStateMachine (autopilot version -> standard modes ->
    // comp info -> params -> mission -> geofence -> rally -> complete) time to
    // run its full course before we fire the PARAM_SET machine explicitly.
    if (!paramSetSent && Date.now() - startMs > 12_000) {
      paramSetSent = true;
      const path = `vehicle.${VEHICLE}.${PARAM_NAME}`;
      send({ type: "getParam", id: nid("get"), vehicleId: VEHICLE, path });
    }
  }

  if (m.type === "paramValue" && m.id?.startsWith("get-")) {
    // Fact::setRawValue() is a no-op (no MAVLink round trip, no PARAM_SET
    // machine, no response) if the new value equals the current one - see
    // src/FactSystem/Fact.cc setRawValue(). Nudge it by 1 so the machine
    // actually fires; still a harmless value for a simulated battery.
    const path = `vehicle.${VEHICLE}.${PARAM_NAME}`;
    const nudged = (typeof m.value === "number" ? m.value : 0) + 1;
    send({ type: "setParam", id: nid("set"), vehicleId: VEHICLE, path, value: nudged });
  }

  if (m.type === "paramValue" && m.id?.startsWith("set-")) {
    paramSetAcked = true;
    console.log("PASS paramSet round-trip complete for", PARAM_NAME);
    clearTimeout(timer);
    // Give the machine's FunctionState/QGCFinalState/finished() chain a beat
    // to log before we tear the connection down.
    setTimeout(() => {
      ws.close();
      process.exit(0);
    }, 1000);
  }
});

ws.addEventListener("error", (e) => { console.error("ws error", e); });
ws.addEventListener("close", () => {
  if (!paramSetAcked) {
    console.error("closed before paramSet round-trip completed");
  }
});
