/**
 * §15 image channel (Q8d): subscribe -> snapshot/stream, schema shape, and a genuine
 * byte-integrity check across the full MockLink -> ImageProtocolManager -> Vehicle ->
 * ImageChannel -> WebBridgeServer -> WebSocket round trip.
 *
 * Requires the target ghost to have been started with `--mock-link` (MockLink's
 * `_sendMockOpticalFlowImage()`, gated on `enableCamera` -- QGCApplication.cc's `--mock-link`
 * wiring always passes `enableCamera=true` specifically so this group has something to observe).
 *
 * Byte-integrity strategy, two layers:
 *
 * 1. ABSOLUTE byte-compare: MockLink::_sendMockOpticalFlowImage() fills each RAW8U byte with
 *    `(seed*17 + offset) & 0xFF`, where `seed` is MockLink's 1Hz tick counter at emission time.
 *    Images are emitted exactly when `tick % 3 == 0` (kMockImageIntervalTicks), the in-process
 *    MockLink link is lossless, and ImageProtocolManager counts every completed image -- so the
 *    N-th image the vehicle assembles (wire `imageIndex` == N) was generated with `seed == 3*N`.
 *    That lets this test reconstruct the ENTIRE expected 64-byte buffer for a received
 *    imageIndex and compare byte-for-byte against the base64-decoded wire payload: a true
 *    "byte-compare with what MockLink sent" across the full MockLink -> ImageProtocolManager ->
 *    Vehicle -> ImageChannel -> WebSocket round trip.
 *
 * 2. Consecutive-delta signature: between two consecutively-received images the per-byte delta
 *    is the constant `(3*17) & 0xFF` at every offset -- a check independent of layer 1's
 *    seed==3N mapping, so if MockLink's emission cadence ever changes, layer 1 failing while
 *    layer 2 still passes reads as "seed model stale", not "bytes corrupted in transit".
 */
import { check, TestConn } from "../lib.ts";

/** Rebuilds the exact 64-byte RAW8U buffer MockLink::_sendMockOpticalFlowImage() generates for
 * `seed` -- see the module doc comment (layer 1). */
function expectedMockImageBytes(seed: number): Buffer {
  const bytes = Buffer.alloc(64);
  for (let offset = 0; offset < 64; offset++) {
    bytes[offset] = (seed * 17 + offset) & 0xff;
  }
  return bytes;
}

export async function runImageGroup(url: string, vehicleId: number): Promise<void> {
  const conn = await TestConn.openAuthed(url);
  const isImage = (m: Record<string, unknown>) => m.type === "image" && m.vehicleId === vehicleId;

  conn.send({ type: "subscribe", id: "im1", channel: "image", vehicleId });
  const subAck = await conn.next("image subscribeAck", (m) => m.type === "subscribeAck" && m.id === "im1");
  check(
    "§15.2 image subscribeAck echoes id/channel/vehicleId",
    subAck.channel === "image" && subAck.vehicleId === vehicleId,
    subAck,
  );

  // The ghost may have been running (and MockLink emitting every ~3s) for a while by the time
  // this group runs, in which case ImageChannel already has a cached image and the snapshot
  // arrives immediately; a freshly-booted ghost needs up to ~3s for the first one. Generous
  // margin for CI/docker overhead either way.
  const first = await conn.next("first image message", isImage, 12000);
  check(
    "§15.3 image message matches the schema (RAW8U 8x8 from MockLink's fixture)",
    typeof first.imageIndex === "number" &&
      first.format === "raw8u" &&
      first.width === 8 &&
      first.height === 8 &&
      typeof first.data === "string" &&
      (first.data as string).length > 0,
    first,
  );

  const firstBytes = Buffer.from(first.data as string, "base64");
  check("§15.3 decoded raw8u payload is exactly width*height bytes", firstBytes.length === 64, firstBytes.length);

  // Layer 1 (module doc comment): absolute byte-compare against a from-scratch reconstruction of
  // what MockLink generated for this imageIndex (seed == 3 * imageIndex).
  const expectedFirst = expectedMockImageBytes(3 * (first.imageIndex as number));
  check(
    "§15 byte-integrity (absolute): wire payload is byte-identical to MockLink's deterministic generation for this imageIndex",
    firstBytes.equals(expectedFirst),
    {
      imageIndex: first.imageIndex,
      receivedSample: Array.from(firstBytes.subarray(0, 8)),
      expectedSample: Array.from(expectedFirst.subarray(0, 8)),
    },
  );

  const second = await conn.next(
    "second image message (imageIndex advanced by exactly 1)",
    (m) => isImage(m) && m.imageIndex === (first.imageIndex as number) + 1,
    8000,
  );
  const secondBytes = Buffer.from(second.data as string, "base64");
  check("§15.3 second image is also exactly width*height bytes", secondBytes.length === 64, secondBytes.length);

  const expectedDelta = (3 * 17) & 0xff; // 51 -- see the module doc comment above
  let deltaOk = secondBytes.length === firstBytes.length;
  for (let i = 0; deltaOk && i < firstBytes.length; i++) {
    const delta = (secondBytes[i]! - firstBytes[i]! + 256) % 256;
    deltaOk = delta === expectedDelta;
  }
  check(
    `§15 byte-integrity: consecutive images differ by the exact deterministic delta (${expectedDelta}) at every one of ${firstBytes.length} byte offsets`,
    deltaOk,
    {
      firstImageIndex: first.imageIndex,
      secondImageIndex: second.imageIndex,
      firstBytesSample: Array.from(firstBytes.subarray(0, 8)),
      secondBytesSample: Array.from(secondBytes.subarray(0, 8)),
    },
  );

  conn.send({ type: "unsubscribe", id: "im2", channel: "image", vehicleId });
  await conn.next("image unsubscribeAck", (m) => m.type === "unsubscribeAck" && m.id === "im2");

  // Re-subscribe restarts seq at 1 (§2.4), same as telemetry/mission.
  conn.send({ type: "subscribe", id: "im3", channel: "image", vehicleId });
  await conn.next("image re-subscribeAck", (m) => m.type === "subscribeAck" && m.id === "im3");
  const resnap = await conn.next("image re-subscribe snapshot", isImage, 12000);
  check("§15/§2.4 re-subscribe restarts seq at 1", resnap.seq === 1, resnap.seq);

  conn.close();
}
