/**
 * §2.2/§2.4 subscribe/unsubscribe lifecycle + §3.1 subscribe/unsubscribe envelope + §10 errors.
 *
 * Covers: subscribeAck echoes id/channel/vehicleId, unknown channel -> UNKNOWN_CHANNEL, missing
 * required field -> BAD_MESSAGE, unsubscribe stops the stream, re-subscribe restarts seq at 1
 * with a fresh snapshot (§2.4's gap-detection re-subscribe contract), and connection survives all
 * of the above. Also covers §3.1's two vehicleId edge cases, both now NORMATIVE (not gaps, as of
 * Wave 15): a `vehicleId` naming a not-yet-tracked vehicle is accepted (late-binding subscribe,
 * §3.1's doc note) while a vehicle-scoped channel with `vehicleId` omitted entirely is BAD_MESSAGE.
 */
import { check, telemetrySchemaOk, TestConn } from "../lib.ts";

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

  // --- §3.1 late-binding subscribe (NORMATIVE as of Wave 15, see PROTOCOL.md §3.1's doc note):
  // a vehicleId not in the currently-tracked vehicle set is accepted (subscribeAck'd) rather than
  // erroring -- it produces no snapshot yet (this bogus id will never actually connect, so "yet"
  // never arrives within the probe window), but the subscription is durably recorded and delivery
  // would begin transparently if/when the vehicle appeared. This is deliberate: the web client's
  // startup handshake (web/src/bridge/session.ts) subscribes telemetry/mission for its configured
  // vehicle immediately after hello, routinely before a MockLink/real vehicle finishes attaching.
  const bogusVehicleId = 999999;
  conn.send({ type: "subscribe", id: "e4", channel: "telemetry", vehicleId: bogusVehicleId });
  const unknownVehicleAck = await conn.next("subscribeAck for bogus vehicleId", (m) => m.type === "subscribeAck" && m.id === "e4");
  const noError = await conn.tryNext((m) => m.type === "error" && m.id === "e4", 300);
  const noSnapshot = await conn.tryNext((m) => m.type === "telemetry" && m.vehicleId === bogusVehicleId, 700);
  check(
    "§3.1 subscribe with a not-yet-tracked vehicleId is accepted (late-binding), not UNKNOWN_VEHICLE",
    unknownVehicleAck.vehicleId === bogusVehicleId && noError === null && noSnapshot === null,
    { ack: unknownVehicleAck, noError, noSnapshot },
  );

  // --- §3/§10: subscribing to a vehicle-scoped channel with vehicleId OMITTED entirely (not just
  // unresolvable) IS a malformed request -- §3's envelope table marks `vehicleId` required "for
  // vehicle-scoped messages", and unlike the late-binding case above there is no id to bind to
  // later. Fixed in Wave 15 (WebBridgeServer::_handleSubscription() now checks channel membership
  // in the vehicle-scoped set before accepting).
  conn.send({ type: "subscribe", id: "e5", channel: "telemetry" }); // vehicleId omitted entirely
  const missingVehicleErr = await conn.next("BAD_MESSAGE for vehicleId omitted", (m) => m.type === "error" && m.id === "e5");
  check(
    "§3/§10 subscribe to a vehicle-scoped channel with vehicleId omitted -> error BAD_MESSAGE",
    missingVehicleErr.code === "BAD_MESSAGE",
    missingVehicleErr,
  );

  // Same rule applies to `mission` (the other vehicle-scoped channel); `video` (streamId-scoped)
  // and `adsb` (unscoped) are deliberately NOT covered by this rule -- see the vehicle-scoped set
  // in WebBridgeServer::_handleSubscription().
  conn.send({ type: "subscribe", id: "e6", channel: "mission" }); // vehicleId omitted entirely
  const missingVehicleErrMission = await conn.next("BAD_MESSAGE for vehicleId omitted (mission)", (m) => m.type === "error" && m.id === "e6");
  check(
    "§3/§10 subscribe to mission with vehicleId omitted -> error BAD_MESSAGE",
    missingVehicleErrMission.code === "BAD_MESSAGE",
    missingVehicleErrMission,
  );

  check("§3.1/§10 connection survives every error/edge case above", conn.isOpen);

  conn.close();
}
