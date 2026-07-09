/**
 * §9 video channel: subscribe -> videoConfig, binary frame header (§9.2's 16-byte LE header,
 * magic 0x4656).
 *
 * Requires a live GStreamer feed reaching the target ghost's configured UDP video source (see
 * run.ts's header comment for how to set one up, e.g. tools/ghost/conformance's own dry run
 * pattern or an already-running instance fed on udp:5600). When no frames show up within the
 * timeout, this group SKIPs cleanly (not a suite failure) unless VIDEO_EXPECTED=1, in which case
 * it's treated as a hard failure -- for CI legs that provision the feed themselves and want to
 * catch a regression that silently drops video.
 */
import { check, skip, TestConn } from "../lib.ts";

export async function runVideoGroup(url: string, streamId: number, videoExpected: boolean): Promise<void> {
  const conn = await TestConn.openAuthed(url);

  conn.send({ type: "subscribe", id: "v1", channel: "video", streamId });
  const vSubAck = await conn.next("video subscribeAck", (m) => m.type === "subscribeAck" && m.id === "v1");
  check("§9.1 video subscribeAck echoes id/channel/streamId", vSubAck.channel === "video" && vSubAck.streamId === streamId, vSubAck);

  const videoConfig = await conn.tryNext((m) => m.type === "videoConfig" && m.streamId === streamId, 3000);
  if (!videoConfig) {
    const msg = `no videoConfig arrived within 3s (no video source feeding this ghost, or GStreamer unavailable) -- target url=${url}`;
    if (videoExpected) {
      check("§9.1 videoConfig arrives after subscribe (VIDEO_EXPECTED=1)", false, msg);
    } else {
      skip("§9 video group (videoConfig/binary frame/keyframe checks)", msg);
    }
    conn.close();
    return;
  }

  check(
    "§9.1 videoConfig carries codec/dimensions/sps/pps",
    videoConfig.channel === "video" &&
      videoConfig.codec === "h264" &&
      typeof videoConfig.width === "number" &&
      typeof videoConfig.height === "number" &&
      typeof videoConfig.sps === "string" &&
      (videoConfig.sps as string).length > 0 &&
      typeof videoConfig.pps === "string" &&
      (videoConfig.pps as string).length > 0,
    videoConfig,
  );

  const frames = await conn.collectBinary(1, 2000);
  if (frames.length === 0) {
    const msg = `videoConfig arrived but no binary frame followed within 2s -- target url=${url}`;
    if (videoExpected) {
      check("§9.2 a binary frame follows videoConfig (VIDEO_EXPECTED=1)", false, msg);
    } else {
      skip("§9.2 binary frame header / keyframe checks", msg);
    }
    conn.close();
    return;
  }

  const first = frames[0]!;
  const dv = new DataView(first.buffer, first.byteOffset, first.byteLength);
  check("§9.2 header magic is 0x4656", dv.getUint16(0, true) === 0x4656, dv.getUint16(0, true).toString(16));
  check("§9.2 header version is 1", dv.getUint8(2) === 1, dv.getUint8(2));
  check("§9.2 header streamId matches the subscribed stream", dv.getUint8(3) === streamId, dv.getUint8(3));
  check("§9.2 header reserved bytes [5..7] are zero-filled", dv.getUint8(5) === 0 && dv.getUint8(6) === 0 && dv.getUint8(7) === 0, [dv.getUint8(5), dv.getUint8(6), dv.getUint8(7)]);
  check("§9.2 frame carries an Annex-B payload beyond the 16-byte header", first.length > 16, first.length);
  const flags = dv.getUint8(4);
  check("§9.2 flags bits 1-7 are reserved (0)", (flags & 0xfe) === 0, flags);

  // --- §9.2: "the first binary frame after videoConfig is a keyframe; a client joining
  // mid-stream renders nothing until the first keyframe." Fixed in Wave 15:
  // WebBridgeServer::broadcastBinary() now gates each client individually
  // (ClientState::videoPendingKeyframe) from the moment it (re)subscribes to a video stream until
  // the next keyframe for that stream is seen, withholding non-keyframe frames from that client
  // only -- see PROTOCOL.md §9.2's per-client enforcement note.
  const isKeyframe = (flags & 0x01) === 1;
  check(
    "§9.2 first binary frame after (re)subscribe is a keyframe (per-client keyframe-wait gate)",
    isKeyframe,
    { observedKeyframeBit: isKeyframe ? 1 : 0, expectedPerSpec: 1 },
  );

  conn.close();
}
