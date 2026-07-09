/**
 * §7 mission channel: subscribe/snapshot, upload/download/clear round-trip, seq==index
 * validation error, unknown-vehicle handling.
 *
 * Runs against a real MockLink vehicle's MissionManager -- upload/clear are real PlanManager
 * transactions (async, correlated), not canned mock echoes.
 */
import { check, gap, TestConn } from "../lib.ts";

export async function runMissionGroup(url: string, vehicleId: number): Promise<void> {
  const conn = await TestConn.openAuthed(url);
  const isMissionState = (m: Record<string, unknown>) => m.type === "missionState" && m.vehicleId === vehicleId;

  // --- §7.2/§2.2: subscribe -> subscribeAck + a missionState snapshot (seq 1, snapshot true).
  conn.send({ type: "subscribe", id: "mi1", channel: "mission", vehicleId });
  const missionSubAck = await conn.next("mission subscribeAck", (m) => m.type === "subscribeAck" && m.id === "mi1");
  check("§7.2 mission subscribeAck echoes id/channel/vehicleId", missionSubAck.channel === "mission" && missionSubAck.vehicleId === vehicleId, missionSubAck);
  const snapshot = await conn.next("mission snapshot", isMissionState);
  check("§2.2/§2.4 mission snapshot has seq 1 + snapshot true", snapshot.seq === 1 && snapshot.snapshot === true, snapshot);
  check(
    "§7 missionState carries currentSeq + an items array",
    typeof snapshot.currentSeq === "number" && Array.isArray(snapshot.items),
    snapshot,
  );

  // --- §7.1/§7.2: missionUpload with a valid 2-item mission -> missionAck accepted, followed by
  // a missionState update on the subscribed stream. NOTE: itemCount is intentionally NOT asserted
  // to equal the uploaded count -- MissionChannel.cc's own doc comment documents that
  // PlanManager::writeMissionItems() silently drops the first (home) item when the firmware
  // doesn't want it sent, so a 2-item upload can legitimately land as itemCount:1. PROTOCOL.md
  // §7.2's example doesn't mention this caveat at all (worth a doc footnote -- see report).
  const uploadItems = [
    { seq: 0, frame: 6, command: 16, current: true, autoContinue: true, param1: 0, param2: 0, param3: 0, param4: 0, lat: 47.398, lon: 8.546, alt: 25 },
    { seq: 1, frame: 6, command: 16, current: false, autoContinue: true, param1: 0, param2: 0, param3: 0, param4: 0, lat: 47.399, lon: 8.547, alt: 30 },
  ];
  conn.send({ type: "missionUpload", id: "mu1", vehicleId, items: uploadItems });
  const uploadAck = await conn.next("missionUpload ack", (m) => m.type === "missionAck" && m.id === "mu1", 5000);
  check(
    "§7.2 missionUpload -> missionAck accepted with a numeric itemCount >= 1",
    uploadAck.status === "accepted" && typeof uploadAck.itemCount === "number" && (uploadAck.itemCount as number) >= 1,
    uploadAck,
  );
  // NOTE: PlanManager fires an intermediate currentIndexChanged(-1) (-> an empty-items
  // missionState) partway through the write transaction, BEFORE the final post-write state --
  // MissionChannel.cc wires both currentIndexChanged and newMissionItemsAvailable to
  // _publishMissionState(). Wait specifically for the non-empty one (the actual post-write
  // state), not just "any missionState", so the stray intermediate broadcast doesn't get
  // mistaken for the real update (and doesn't linger unconsumed to confuse a later assertion).
  const afterUpload = await conn.next("missionState update after upload (non-empty)", (m) => isMissionState(m) && Array.isArray(m.items) && (m.items as unknown[]).length >= 1, 3000);
  check(
    "§7.2 missionState update after upload reflects a non-empty mission",
    Array.isArray(afterUpload.items) && (afterUpload.items as unknown[]).length >= 1,
    afterUpload.items,
  );
  // Drain any other stray missionState broadcasts (e.g. the intermediate empty one above) so they
  // can't be mistaken for a later step's update.
  await conn.collect(isMissionState, 100);

  // --- §7.2: missionDownload answers with missionItems matching the current (cached) mission.
  conn.send({ type: "missionDownload", id: "md1", vehicleId });
  const downloadResp = await conn.next("missionDownload response", (m) => m.type === "missionItems" && m.id === "md1", 3000);
  check(
    "§7.2 missionDownload -> missionItems array matches the schema",
    Array.isArray(downloadResp.items) &&
      (downloadResp.items as Record<string, unknown>[]).every(
        (it) =>
          typeof it.seq === "number" &&
          typeof it.frame === "number" &&
          typeof it.command === "number" &&
          typeof it.current === "boolean" &&
          typeof it.autoContinue === "boolean" &&
          typeof it.param1 === "number" &&
          typeof it.lat === "number" &&
          typeof it.lon === "number" &&
          typeof it.alt === "number",
      ),
    downloadResp.items,
  );

  // --- §7.2 validation error: seq must equal array position (MissionChannel.cc's
  // _parseAndValidateItems -- purely synchronous, no vehicle round-trip, so no timeout risk).
  conn.send({
    type: "missionUpload",
    id: "mu-badseq",
    vehicleId,
    items: [{ seq: 5, frame: 6, command: 16, current: true, autoContinue: true, param1: 0, param2: 0, param3: 0, param4: 0, lat: 1, lon: 1, alt: 1 }],
  });
  const badSeqAck = await conn.next("bad-seq missionUpload ack", (m) => m.type === "missionAck" && m.id === "mu-badseq", 2000);
  check(
    "§7.2 seq != array position -> missionAck rejected with a reason mentioning seq",
    badSeqAck.status === "rejected" && typeof badSeqAck.reason === "string" && (badSeqAck.reason as string).toLowerCase().includes("seq"),
    badSeqAck,
  );

  // --- §7.2: missionClear -> missionAck accepted itemCount 0, missionState update empties out.
  // Drain any stragglers left over from the upload step first (defensive; the upload step above
  // already drains its own), then wait specifically for an EMPTY missionState -- same rationale
  // as the upload assertion above: don't let "any missionState" match a stale buffered message.
  await conn.collect(isMissionState, 50);
  conn.send({ type: "missionClear", id: "mc1", vehicleId });
  const clearAck = await conn.next("missionClear ack", (m) => m.type === "missionAck" && m.id === "mc1", 5000);
  check("§7.2 missionClear -> missionAck accepted itemCount 0", clearAck.status === "accepted" && clearAck.itemCount === 0, clearAck);
  const afterClear = await conn.next("missionState update after clear (empty)", (m) => isMissionState(m) && Array.isArray(m.items) && (m.items as unknown[]).length === 0, 3000);
  check("§7.2 missionState update after clear has an empty item list", Array.isArray(afterClear.items) && (afterClear.items as unknown[]).length === 0, afterClear.items);

  // --- GAP: mission message to an unknown vehicle -- same pattern as command.ts. §10's
  // UNKNOWN_VEHICLE error is not used here either; MissionChannel answers with an ordinary
  // rejected missionAck reason "Unknown vehicle" instead (handleMissionMessage()'s early-return
  // when `!vehicle || !vehicle->missionManager()`).
  const bogusVehicleId = 999999;
  conn.send({ type: "missionDownload", id: "md-bogus", vehicleId: bogusVehicleId });
  const bogusResp = await conn.next("response for unknown-vehicle missionDownload", (m) => (m.type === "missionAck" || m.type === "error") && m.id === "md-bogus", 2000);
  gap(
    "§10 GAP: mission message to unknown vehicle answers with rejected missionAck, not error UNKNOWN_VEHICLE",
    bogusResp.type === "missionAck" && bogusResp.status === "rejected" && bogusResp.reason === "Unknown vehicle",
    bogusResp,
  );

  conn.close();
}
