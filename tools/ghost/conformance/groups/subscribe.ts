/**
 * §2.2/§2.4 subscribe/unsubscribe lifecycle + §3.1 subscribe/unsubscribe envelope + §10 errors.
 *
 * Covers: subscribeAck echoes id/channel/vehicleId, unknown channel -> UNKNOWN_CHANNEL, missing
 * required field -> BAD_MESSAGE, unsubscribe stops the stream, re-subscribe restarts seq at 1
 * with a fresh snapshot (§2.4's gap-detection re-subscribe contract), and connection survives all
 * of the above. Also documents a real conformance gap: §3.1 says "unknown vehicle -> error
 * UNKNOWN_VEHICLE" but the telemetry/mission channels silently subscribeAck an unknown vehicleId
 * and then never send a snapshot (no error, ever) -- see the gap() call below.
 */
import { check, gap, telemetrySchemaOk, TestConn } from "../lib.ts";

export async function runSubscribeGroup(url: string, vehicleId: number): Promise<void> {
  const conn = await TestConn.openAuthed(url);
  const isTelemetry = (m: Record<string, unknown>) => m.type === "telemetry" && m.vehicleId === vehicleId;

  // --- §3.1: subscribeAck echoes id/channel/vehicleId, followed immediately by a snapshot
  // (seq 1, snapshot true) -- §2.2's "on every successful subscribe ... one full-state snapshot".
  conn.send({ type: "subscribe", id: "s1", channel: "telemetry", vehicleId });
  const subAck = await conn.next("subscribeAck", (m) => m.type === "subscribeAck" && m.id === "s1");
  check("§3.1 subscribeAck echoes id/channel/vehicleId", subAck.channel === "telemetry" && subAck.vehicleId === vehicleId, subAck);
  const snapshot = await conn.next("telemetry snapshot", isTelemetry);
  check("§2.2/§2.4 snapshot has seq 1 + snapshot true", snapshot.seq === 1 && snapshot.snapshot === true, { seq: snapshot.seq, snapshot: snapshot.snapshot });
  check("§4 snapshot matches the telemetry schema", telemetrySchemaOk(snapshot, vehicleId).ok, telemetrySchemaOk(snapshot, vehicleId));

  // --- §2.2: "Subscribing is idempotent; re-subscribing re-sends the snapshot." §2.4: "the
  // server responds with a fresh snapshot (whose seq restarts at 1)."
  await conn.collect(isTelemetry, 250); // let a few stream messages accumulate first
  conn.send({ type: "subscribe", id: "s1b", channel: "telemetry", vehicleId });
  await conn.next("re-subscribeAck", (m) => m.type === "subscribeAck" && m.id === "s1b");
  const resnap = await conn.next("idempotent re-subscribe snapshot", isTelemetry);
  check("§2.2/§2.4 idempotent re-subscribe restarts seq at 1 with a fresh snapshot", resnap.seq === 1 && resnap.snapshot === true, resnap);

  // --- §3.1: "unsubscribe" stops the stream.
  conn.send({ type: "unsubscribe", id: "u1", channel: "telemetry", vehicleId });
  const unsubAck = await conn.next("unsubscribeAck", (m) => m.type === "unsubscribeAck" && m.id === "u1");
  check("§3.1 unsubscribeAck echoes id/channel/vehicleId", unsubAck.channel === "telemetry" && unsubAck.vehicleId === vehicleId, unsubAck);
  await conn.collect(isTelemetry, 100); // drain anything already in flight before the ack landed
  const afterUnsub = await conn.collect(isTelemetry, 400);
  check("§3.1 unsubscribe actually stops the stream", afterUnsub.length === 0, afterUnsub.length);

  // --- §2.4: re-subscribing after an explicit unsubscribe also restarts seq at 1.
  conn.send({ type: "subscribe", id: "s2", channel: "telemetry", vehicleId });
  await conn.next("re-subscribeAck after unsubscribe", (m) => m.type === "subscribeAck" && m.id === "s2");
  const resnap2 = await conn.next("post-unsubscribe re-subscribe snapshot", isTelemetry);
  check("§2.4 re-subscribe after unsubscribe restarts seq at 1", resnap2.seq === 1 && resnap2.snapshot === true, resnap2);

  // --- §3.1 error paths; connection must stay open throughout.
  const isErr = (id: string) => (m: Record<string, unknown>) => m.type === "error" && m.id === id;

  conn.send({ type: "subscribe", id: "e1", channel: "warpField", vehicleId });
  check("§3.1 unknown channel -> error UNKNOWN_CHANNEL", (await conn.next("e1", isErr("e1"))).code === "UNKNOWN_CHANNEL");

  conn.send({ type: "subscribe", id: "e3" }); // missing channel entirely
  check("§10 missing required field (channel) -> error BAD_MESSAGE", (await conn.next("e3", isErr("e3"))).code === "BAD_MESSAGE");

  conn.send("not json at all {");
  const badJson = await conn.next("BAD_MESSAGE (unparseable)", (m) => m.type === "error" && m.id === undefined);
  check("§10 unparseable JSON -> error BAD_MESSAGE", badJson.code === "BAD_MESSAGE", badJson);

  check("§3.1/§10 connection survives every error above", conn.isOpen);

  // --- §3.1 GAP: "unknown vehicle -> error UNKNOWN_VEHICLE" is explicit normative text. The real
  // bridge instead subscribeAcks an unknown vehicleId unconditionally (WebBridgeServer.cc's
  // _handleSubscription() never validates the scope id against MultiVehicleManager) and the
  // owning channel's sendSnapshot() (TelemetryChannel.cc / MissionChannel.cc) just silently
  // no-ops ("qCDebug ... unknown vehicleId") when it can't find the vehicle -- no error, no
  // snapshot, ever. FactChannel (getParam/setParam) DOES implement this correctly (see
  // command.ts's/mission.ts's matching gap() calls for the same pattern on those channels).
  const bogusVehicleId = 999999;
  conn.send({ type: "subscribe", id: "e4", channel: "telemetry", vehicleId: bogusVehicleId });
  const unknownVehicleAck = await conn.next("subscribeAck for bogus vehicleId", (m) => m.type === "subscribeAck" && m.id === "e4");
  const noError = await conn.tryNext((m) => m.type === "error" && m.id === "e4", 300);
  const noSnapshot = await conn.tryNext((m) => m.type === "telemetry" && m.vehicleId === bogusVehicleId, 700);
  gap(
    "§3.1 GAP: subscribe with unknown vehicleId should error UNKNOWN_VEHICLE, but real bridge subscribeAcks + goes silent (no error, no snapshot, ever)",
    unknownVehicleAck.vehicleId === bogusVehicleId && noError === null && noSnapshot === null,
    { ack: unknownVehicleAck, noError, noSnapshot },
  );

  // --- Same root cause, different trigger: subscribing to a vehicle-scoped channel with
  // vehicleId OMITTED entirely (not just wrong) is not rejected either. §3's envelope table
  // marks `vehicleId` required "for vehicle-scoped messages", which subscribe/telemetry clearly
  // is -- a strict reading says this should be BAD_MESSAGE (§10: "missing required field"). The
  // real bridge instead treats a missing vehicleId exactly like an unknown one: subscribeAck
  // (with no vehicleId field, since WebBridgeServer.cc's `hasVehicleId` is false) and then
  // silence forever (TelemetryChannel::sendSnapshot(kNoVehicleId) finds no vehicle either).
  conn.send({ type: "subscribe", id: "e5", channel: "telemetry" }); // vehicleId omitted entirely
  const missingVehicleAck = await conn.next("subscribeAck with vehicleId omitted", (m) => m.type === "subscribeAck" && m.id === "e5");
  const noErrorMissing = await conn.tryNext((m) => m.type === "error" && m.id === "e5", 300);
  const noSnapshotMissing = await conn.tryNext((m) => m.type === "telemetry" && m.vehicleId === undefined, 700);
  gap(
    "§3/§10 GAP: subscribe to a vehicle-scoped channel with vehicleId omitted should be BAD_MESSAGE, but real bridge subscribeAcks + goes silent",
    missingVehicleAck.vehicleId === undefined && noErrorMissing === null && noSnapshotMissing === null,
    { ack: missingVehicleAck, noErrorMissing, noSnapshotMissing },
  );

  conn.close();
}
