/**
 * Multi-client isolation: §14.3's per-connection welcome, §11.1's broadcast-to-all tick, §3
 * subscription scoping (one client's subscribe/unsubscribe never affects another client's view).
 */
import { check, notificationSchemaOk, TestConn } from "../lib.ts";

export async function runMultiClientGroup(url: string, vehicleId: number): Promise<void> {
  // --- §14.3: each new connection gets its OWN welcome notification (explicitly exempt from the
  // §14.2 1s dedup window, even though the text is identical and these connect close together).
  const s1 = await TestConn.open(url);
  s1.send({ type: "hello", id: "h1", token: "dev-token", protocolVersion: "0.1" });
  await s1.next("s1 helloAck", (m) => m.type === "helloAck");
  const s1Welcome = await s1.next("s1 welcome notification", (m) => m.type === "notification", 1500);

  const s2 = await TestConn.open(url);
  s2.send({ type: "hello", id: "h1", token: "dev-token", protocolVersion: "0.1" });
  await s2.next("s2 helloAck", (m) => m.type === "helloAck");
  const s2Welcome = await s2.next("s2 welcome notification", (m) => m.type === "notification", 1500);

  check(
    "§14.3 each connection gets its own welcome notification (not deduped against the other's)",
    notificationSchemaOk(s1Welcome) && notificationSchemaOk(s2Welcome) && s1Welcome.text === "Ghost bridge ready" && s2Welcome.text === "Ghost bridge ready",
    { s1Welcome, s2Welcome },
  );

  // --- §3 subscription scoping: S1 subscribes telemetry, S2 does not -- only S1 streams it.
  s1.send({ type: "subscribe", id: "sub1", channel: "telemetry", vehicleId });
  await s1.next("s1 subscribeAck", (m) => m.type === "subscribeAck" && m.id === "sub1");
  await s1.next("s1 telemetry snapshot", (m) => m.type === "telemetry" && m.vehicleId === vehicleId);

  const [s1Stream, s2Stream] = await Promise.all([
    s1.collect((m) => m.type === "telemetry" && m.vehicleId === vehicleId, 600),
    s2.collect((m) => m.type === "telemetry" && m.vehicleId === vehicleId, 600),
  ]);
  check(`§3 subscribed client (S1) receives telemetry (got ${s1Stream.length})`, s1Stream.length >= 3, s1Stream.length);
  check("§3 unsubscribed client (S2) receives NO telemetry for a channel it never subscribed to", s2Stream.length === 0, s2Stream.length);

  // --- §11.1: tick is broadcastAll -- both connections see it, subscribed or not.
  const [s1Tick, s2Tick] = await Promise.all([
    s1.next("s1 tick", (m) => m.type === "tick", 1600),
    s2.next("s2 tick", (m) => m.type === "tick", 1600),
  ]);
  check("§11.1 tick broadcast reaches BOTH connected clients", s1Tick.type === "tick" && s2Tick.type === "tick", { s1Tick, s2Tick });

  // --- Unsubscribe on S1 doesn't disturb S2: S2 keeps ticking, and can independently subscribe
  // its own telemetry stream afterward with no interference from S1's teardown.
  //
  // Drain S1's telemetry backlog first: the tick-wait above (up to 1.6s) let ~10Hz telemetry
  // accumulate unconsumed in s1.received (next() only removes the one message it matched, i.e.
  // the tick) -- without draining, those STALE pre-unsubscribe messages would still be sitting in
  // the buffer when we later measure "no telemetry after unsubscribe", producing a false failure.
  await s1.collect((m) => m.type === "telemetry" && m.vehicleId === vehicleId, 0);
  s1.send({ type: "unsubscribe", id: "unsub1", channel: "telemetry", vehicleId });
  await s1.next("s1 unsubscribeAck", (m) => m.type === "unsubscribeAck" && m.id === "unsub1");
  // Drain anything already in flight before the server processed the unsubscribe (mirrors
  // subscribe.ts's identical drain-then-measure pattern).
  await s1.collect((m) => m.type === "telemetry" && m.vehicleId === vehicleId, 100);

  s2.send({ type: "subscribe", id: "sub2", channel: "telemetry", vehicleId });
  await s2.next("s2 subscribeAck", (m) => m.type === "subscribeAck" && m.id === "sub2");
  const s2Snapshot = await s2.next("s2 telemetry snapshot (unaffected by S1's unsubscribe)", (m) => m.type === "telemetry" && m.vehicleId === vehicleId, 2000);
  check("§3 S1 unsubscribing doesn't disturb S2's independent subscription", s2Snapshot.seq === 1 && s2Snapshot.snapshot === true, s2Snapshot);

  const s2StreamAfter = await s2.collect((m) => m.type === "telemetry" && m.vehicleId === vehicleId, 400);
  check("§3 S2's stream keeps flowing after S1 unsubscribed its own", s2StreamAfter.length >= 1, s2StreamAfter.length);
  const s1StreamAfter = await s1.collect((m) => m.type === "telemetry" && m.vehicleId === vehicleId, 400);
  check("§3 S1 itself sees no more telemetry after its own unsubscribe", s1StreamAfter.length === 0, s1StreamAfter.length);

  s1.close();
  s2.close();
}
