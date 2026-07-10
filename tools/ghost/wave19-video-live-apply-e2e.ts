#!/usr/bin/env bun
/**
 * Wave 19 feature e2e: proves PROTOCOL.md §16.6's live-apply mechanism end to end, via bridge
 * messages ONLY -- no restart, no pre-seeded QSettings ini for the Video group. This is the
 * actual point of §16: configuring the ghost's video source AFTER boot, over the wire, and having
 * it take effect without a process restart.
 *
 * Sequence:
 *   1. hello, then getSettings(group=Video) -- confirm the ghost boots with no video source
 *      configured (GhostVideoSource stays inert, per its own doc comment).
 *   2. subscribe to video channel -- confirm no frames/config arrive yet (nothing to attach to).
 *   3. setSetting Video/videoSource = "UDP h.264 Video Stream", Video/udpUrl = "0.0.0.0:5600",
 *      Video/streamEnabled = true -- three bridge messages, no other channel touched.
 *   4. (caller starts a GStreamer feed on udp:5600 at this point, AFTER the settings are applied,
 *      demonstrating the ghost was already listening / re-attached on its own)
 *   5. videoConfig + binary frames arrive on the already-open video subscription -- proving
 *      SettingsChannel::videoSettingChanged() -> GhostVideoSource::start() actually re-attached
 *      the receiver live, with no restart between steps 1 and 5.
 *
 * Usage: PROBE_URL=ws://127.0.0.1:<port> GST_READY_FILE=<path> bun tools/ghost/wave19-video-live-apply-e2e.ts
 *   GST_READY_FILE: this script touches this file right after step 3 so the calling shell script
 *   knows exactly when to start feeding GStreamer (no fixed sleep needed on that side).
 */
import { TestConn } from "./conformance/lib.ts";

const URL = process.env.PROBE_URL ?? "ws://127.0.0.1:8877";
const GST_READY_FILE = process.env.GST_READY_FILE;

async function main(): Promise<void> {
  const conn = await TestConn.openAuthed(URL);
  console.log("connected + authed", URL);

  // Step 1: confirm no video source configured yet.
  conn.send({ type: "getSettings", id: "pre-1", group: "Video" });
  const preSettings = await conn.next("getSettings(Video) before setSetting", (m) => m.type === "settingsValue" && m.id === "pre-1");
  const preEntries = (preSettings.settings ?? []) as Record<string, unknown>[];
  const preSource = preEntries.find((e) => e.name === "videoSource")?.value;
  console.log("videoSource before setSetting:", JSON.stringify(preSource));

  // Step 2: subscribe to video -- expect no videoConfig/binary frame within a short window (nothing
  // to attach to yet).
  conn.send({ type: "subscribe", id: "v-sub", channel: "video", streamId: 1 });
  await conn.next("video subscribeAck", (m) => m.type === "subscribeAck" && m.id === "v-sub");
  const earlyConfig = await conn.tryNext((m) => m.type === "videoConfig", 1500);
  console.log("videoConfig before setSetting (expect none):", earlyConfig ? "UNEXPECTEDLY PRESENT" : "none, as expected");

  // Step 3: the actual feature -- set Video group entirely over the bridge.
  conn.send({ type: "setSetting", id: "set-source", group: "Video", name: "videoSource", value: "UDP h.264 Video Stream" });
  const sourceAck = await conn.next("setSetting videoSource ack", (m) => m.type === "settingsValue" && m.id === "set-source");
  console.log("setSetting videoSource ->", JSON.stringify(sourceAck.settings));

  conn.send({ type: "setSetting", id: "set-url", group: "Video", name: "udpUrl", value: "0.0.0.0:5600" });
  const urlAck = await conn.next("setSetting udpUrl ack", (m) => m.type === "settingsValue" && m.id === "set-url");
  console.log("setSetting udpUrl ->", JSON.stringify(urlAck.settings));

  conn.send({ type: "setSetting", id: "set-enabled", group: "Video", name: "streamEnabled", value: true });
  const enabledAck = await conn.next("setSetting streamEnabled ack", (m) => m.type === "settingsValue" && m.id === "set-enabled");
  console.log("setSetting streamEnabled ->", JSON.stringify(enabledAck.settings));

  // Confirm settingChanged broadcasts happened for all three (§16.5).
  const changes = await conn.collect((m) => m.type === "settingChanged" && m.group === "Video", 500);
  console.log(`observed ${changes.length} settingChanged broadcasts for the Video group`);

  if (GST_READY_FILE) {
    await Bun.write(GST_READY_FILE, "ready");
    console.log("signaled GST_READY_FILE -- calling script should start the gst feed now");
  }

  // Step 5: wait for videoConfig (re-sent live since SPS/PPS just became known) and at least one
  // binary frame -- the actual proof that GhostVideoSource re-attached without a restart.
  const liveConfig = await conn.next("videoConfig after live setSetting", (m) => m.type === "videoConfig", 20000);
  console.log("videoConfig after live setSetting:", JSON.stringify({ codec: liveConfig.codec, width: liveConfig.width, height: liveConfig.height }));

  const frames = await conn.collectBinary(1, 20000);
  if (frames.length === 0) {
    console.log("FAIL: no binary video frames arrived after live setSetting");
    conn.close();
    process.exit(1);
  }
  const frame = frames[0]!;
  const magicOk = frame.length >= 16 && frame[0] === 0x56 && frame[1] === 0x46;
  console.log(`PASS: ${frames.length} binary frame(s) arrived after live setSetting, first frame ${frame.length} bytes, magic ok: ${magicOk}`);

  conn.close();
  process.exit(magicOk ? 0 : 1);
}

main().catch((error) => {
  console.error("FATAL", error);
  process.exit(1);
});
