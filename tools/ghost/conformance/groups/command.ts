/**
 * §5 command channel: request/ack correlation, a rejected case, unknown-vehicle handling.
 *
 * Runs a real arm -> (double-arm rejected) -> disarm sequence against a live MockLink vehicle, so
 * this exercises CommandChannel's actual MAV_COMMAND_ACK correlation path (§5.2's "commandAck
 * carries status/reason/mavResult"), not a canned mock response.
 *
 * The MockLink vehicle is a long-lived process-wide singleton, not per-connection state -- it
 * persists across separate invocations of this suite against the same ghost. This group is
 * therefore written to be self-resetting (a leading, tolerant "make sure we start disarmed" step)
 * rather than assuming a known starting armed state.
 */
import { check, TestConn } from "../lib.ts";

/** Waits for a FRESH telemetry sample confirming `armed`. Drains any already-buffered telemetry
 * first: `TestConn.next()` does a `findIndex` over the whole buffer, so a stale sample from
 * *before* the state transition we're waiting on (e.g. leftover from a previous group, or from
 * before the command that's supposed to cause this transition was even sent) can otherwise
 * satisfy the predicate instantly and falsely "confirm" a transition that hasn't happened yet. */
async function confirmArmed(conn: TestConn, vehicleId: number, armed: boolean, timeoutMs: number): Promise<Record<string, unknown>> {
  const isTelemetry = (m: Record<string, unknown>) => m.type === "telemetry" && m.vehicleId === vehicleId;
  await conn.collect(isTelemetry, 0); // discard the backlog -- only a NEW sample may match below
  return conn.next(`telemetry confirms armed=${armed}`, (m) => isTelemetry(m) && m.armed === armed, timeoutMs);
}

export async function runCommandGroup(url: string, vehicleId: number): Promise<void> {
  const conn = await TestConn.openAuthed(url);

  conn.send({ type: "subscribe", id: "c-tel", channel: "telemetry", vehicleId });
  await conn.next("telemetry subscribeAck", (m) => m.type === "subscribeAck" && m.id === "c-tel");

  // --- Self-reset: force a known starting state (disarmed) regardless of what a previous run (or
  // any other client) left the vehicle in. Tolerant of either outcome ("accepted" if it was
  // armed, "rejected: already disarmed" if it wasn't) -- only the resulting telemetry state is
  // asserted on, not the ack's status.
  conn.send({ type: "command", id: "c-pre-disarm", vehicleId, action: "disarm", params: {} });
  await conn.next("pre-test disarm response", (m) => m.type === "commandAck" && m.id === "c-pre-disarm", 8000);
  await confirmArmed(conn, vehicleId, false, 5000);

  // --- §5.1/§5.2: arm -> commandAck accepted, mavResult present (0 = MAV_RESULT_ACCEPTED). Wide
  // timeout: real autopilot round-trip, not instantaneous like the mock.
  conn.send({ type: "command", id: "c-arm1", vehicleId, action: "arm", params: {} });
  const armAck = await conn.next("arm commandAck", (m) => m.type === "commandAck" && m.id === "c-arm1", 8000);
  check("§5.2 arm -> commandAck accepted, mavResult 0", armAck.status === "accepted" && armAck.mavResult === 0 && armAck.vehicleId === vehicleId, armAck);

  // commandAck accepted only means the autopilot ACKed the command -- Vehicle::armed() itself
  // updates from a subsequent HEARTBEAT, which can lag the ack by up to a heartbeat interval.
  // GuidedActionGate's "already armed" rejection (below) reads the live armed Fact, so firing the
  // double-arm attempt before that Fact has actually flipped would let it through as a second
  // legitimate arm instead of a rejection -- wait for a FRESH confirmation first.
  await confirmArmed(conn, vehicleId, true, 5000);

  // --- §5.2 rejected case: arming an already-armed vehicle is rejected immediately (gated
  // client-side by GuidedActionGate before any autopilot round-trip -- deterministic, no timeout
  // risk), with a human-readable reason and no mavResult (autopilot never answered this one).
  conn.send({ type: "command", id: "c-arm2", vehicleId, action: "arm", params: {} });
  const doubleArmAck = await conn.next("double-arm commandAck", (m) => m.type === "commandAck" && m.id === "c-arm2", 2000);
  check(
    "§5.2 rejected case: arming an already-armed vehicle -> rejected + reason, no mavResult",
    doubleArmAck.status === "rejected" && typeof doubleArmAck.reason === "string" && doubleArmAck.mavResult === undefined,
    doubleArmAck,
  );

  // --- disarm -> accepted, mavResult 0.
  conn.send({ type: "command", id: "c-disarm1", vehicleId, action: "disarm", params: {} });
  const disarmAck = await conn.next("disarm commandAck", (m) => m.type === "commandAck" && m.id === "c-disarm1", 8000);
  check("§5.2 disarm -> commandAck accepted, mavResult 0", disarmAck.status === "accepted" && disarmAck.mavResult === 0, disarmAck);

  // --- Telemetry actually reflects the arm/disarm round-trip (§5.2: "track completion via
  // telemetry armed/flightMode/altRel"). Same heartbeat-lag reasoning as above.
  const postDisarmTelemetry = await confirmArmed(conn, vehicleId, false, 5000);
  check("§5.2 telemetry.armed reflects the disarm that just completed", postDisarmTelemetry.armed === false, postDisarmTelemetry.armed);

  // --- Unknown action -> rejected ack, not a §10 protocol error (BAD_MESSAGE is reserved for
  // envelope shape problems per CommandChannel.cc's own doc comment).
  conn.send({ type: "command", id: "c-unknown-action", vehicleId, action: "selfDestruct", params: {} });
  const unknownActionAck = await conn.next("unknown-action commandAck", (m) => m.type === "commandAck" && m.id === "c-unknown-action", 2000);
  check("§5 unknown action -> rejected commandAck (not a §10 error)", unknownActionAck.status === "rejected" && typeof unknownActionAck.reason === "string", unknownActionAck);

  // --- §10: command to an unknown vehicle. Fixed in Wave 15 for consistency with FactChannel
  // (getParam/setParam), which already answered this condition with a §10 error envelope:
  // CommandChannel now does too (UNKNOWN_VEHICLE), instead of the old rejected-commandAck shape.
  const bogusVehicleId = 999999;
  conn.send({ type: "command", id: "c-bogus-vehicle", vehicleId: bogusVehicleId, action: "arm", params: {} });
  const bogusVehicleAck = await conn.next("response for unknown-vehicle command", (m) => (m.type === "commandAck" || m.type === "error") && m.id === "c-bogus-vehicle", 2000);
  check(
    "§10 command to unknown vehicle -> error UNKNOWN_VEHICLE (consistent with FactChannel)",
    bogusVehicleAck.type === "error" && bogusVehicleAck.code === "UNKNOWN_VEHICLE" && bogusVehicleAck.vehicleId === bogusVehicleId,
    bogusVehicleAck,
  );

  conn.close();
}
