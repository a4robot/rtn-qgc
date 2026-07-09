/**
 * §4 telemetry channel: shape/nullable discipline, ~10 Hz rate, contiguous seq.
 *
 * Uses a real MockLink vehicle (VEHICLE_ID, default 128) so every field is live autopilot state,
 * not a canned fixture -- this is the whole point of testing the real bridge instead of the mock.
 */
import { check, telemetrySchemaOk, TestConn } from "../lib.ts";

export async function runTelemetryGroup(url: string, vehicleId: number): Promise<void> {
  const conn = await TestConn.openAuthed(url);
  const isTelemetry = (m: Record<string, unknown>) => m.type === "telemetry" && m.vehicleId === vehicleId;

  conn.send({ type: "subscribe", id: "t1", channel: "telemetry", vehicleId });
  await conn.next("subscribeAck", (m) => m.type === "subscribeAck" && m.id === "t1");
  const snapshot = await conn.next("snapshot", isTelemetry);
  const snapCheck = telemetrySchemaOk(snapshot, vehicleId);
  check("§4 snapshot: every field present, nullable discipline honored", snapCheck.ok, { snapCheck, snapshot });

  // --- §4: "Rate: ~10 Hz while subscribed." Sample a 1.2s window (snapshot already consumed).
  const stream = await conn.collect(isTelemetry, 1200);
  check(`§4 ~10 Hz stream rate sanity (got ${stream.length} msgs in 1.2s, want 8-16)`, stream.length >= 8 && stream.length <= 16, stream.length);

  // --- §2.4: seq is monotonic and contiguous per (channel, vehicleId) stream, starting at 1 with
  // the snapshot (already consumed as seq 1 above, so this window should start at 2).
  const seqs = stream.map((m) => m.seq as number);
  const contiguous = seqs.every((s, i) => s === 2 + i);
  check("§2.4 seq contiguous from the snapshot (2, 3, 4, ...)", contiguous, seqs);
  check("§4 every streamed message is snapshot:false (only the first is true)", stream.every((m) => m.snapshot === false), stream.map((m) => m.snapshot));

  // --- §4: every message is a FULL snapshot (all fields always present, not a sparse patch).
  const allOk = stream.map((m) => telemetrySchemaOk(m, vehicleId));
  check("§4 every streamed message matches the full schema (not a sparse patch)", allOk.every((r) => r.ok), allOk.filter((r) => !r.ok));

  conn.close();
}
