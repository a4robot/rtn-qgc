/**
 * Throwaway end-to-end conformance check for the mock server, asserting the
 * PROTOCOL.md §12 M1 checklist plus the commandAck lifecycle.
 *
 * Spawns `bun mock/server.ts` itself, connects with plain WebSocket clients,
 * and prints PASS/FAIL per assertion (exit code 0 only if all pass).
 *
 * Run: bun run mock:selftest
 */

const URL_ = `ws://127.0.0.1:${process.env.MOCK_PORT ?? 8877}/`;
/** Mirrors server.ts's own check — selftest spawns the server with the same env/argv, so this must match. */
const FIXTURE_MODE = process.env.MOCK_FIXTURE === "1" || process.argv.includes("--fixture");

let passed = 0;
let failed = 0;

function check(name: string, cond: boolean, detail?: unknown): void {
  if (cond) {
    passed += 1;
    console.log(`PASS ${name}`);
  } else {
    failed += 1;
    console.error(`FAIL ${name}`, detail === undefined ? "" : JSON.stringify(detail));
  }
}

/** Minimal test connection: buffers parsed messages, supports predicate waits. */
class TestConn {
  readonly received: Record<string, unknown>[] = [];
  /** Raw binary WS frames (video, §9.2), in arrival order. */
  readonly binaryReceived: Uint8Array[] = [];
  closeCode: number | null = null;
  private readonly socket: WebSocket;

  private constructor(socket: WebSocket) {
    this.socket = socket;
    socket.binaryType = "arraybuffer";
    socket.onmessage = (event: MessageEvent) => {
      if (typeof event.data === "string") {
        this.received.push(JSON.parse(event.data) as Record<string, unknown>);
      } else if (event.data instanceof ArrayBuffer) {
        this.binaryReceived.push(new Uint8Array(event.data));
      }
    };
    socket.onclose = (event: CloseEvent) => {
      this.closeCode = event.code;
    };
  }

  static async open(): Promise<TestConn> {
    const socket = new WebSocket(URL_);
    await new Promise<void>((resolve, reject) => {
      socket.onopen = () => resolve();
      socket.onerror = () => reject(new Error(`cannot connect to ${URL_}`));
    });
    return new TestConn(socket);
  }

  /** Wait until at least `count` binary frames have arrived, returning (and consuming) all buffered so far. */
  async collectBinary(count: number, timeoutMs = 3000): Promise<Uint8Array[]> {
    const deadline = Date.now() + timeoutMs;
    while (this.binaryReceived.length < count && Date.now() < deadline) {
      await Bun.sleep(20);
    }
    return this.binaryReceived.splice(0, this.binaryReceived.length);
  }

  get isOpen(): boolean {
    return this.socket.readyState === WebSocket.OPEN;
  }

  send(message: string | Record<string, unknown>): void {
    this.socket.send(typeof message === "string" ? message : JSON.stringify(message));
  }

  /** Wait for (and consume) the first buffered message matching `pred`. */
  async next(
    label: string,
    pred: (m: Record<string, unknown>) => boolean,
    timeoutMs = 3000,
  ): Promise<Record<string, unknown>> {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      const i = this.received.findIndex(pred);
      if (i >= 0) {
        return this.received.splice(i, 1)[0]!;
      }
      await Bun.sleep(20);
    }
    throw new Error(`timeout waiting for ${label}`);
  }

  /** Collect (and consume) all messages matching `pred` over `windowMs`. */
  async collect(
    pred: (m: Record<string, unknown>) => boolean,
    windowMs: number,
  ): Promise<Record<string, unknown>[]> {
    await Bun.sleep(windowMs);
    const matches = this.received.filter(pred);
    for (const m of matches) {
      this.received.splice(this.received.indexOf(m), 1);
    }
    return matches;
  }

  async waitClosed(timeoutMs = 3000): Promise<number> {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      if (this.closeCode !== null) {
        return this.closeCode;
      }
      await Bun.sleep(20);
    }
    throw new Error("timeout waiting for close");
  }

  close(): void {
    this.socket.close();
  }
}

const isTelemetry = (m: Record<string, unknown>) => m.type === "telemetry";
const NOTIFICATION_SEVERITIES = new Set(["info", "warning", "critical"]);

/** Shape check for the `notification` push contract: {type, severity, text, timeUs, vehicleId?}. */
function notificationSchemaOk(m: Record<string, unknown>): boolean {
  return (
    m.type === "notification" &&
    typeof m.severity === "string" &&
    NOTIFICATION_SEVERITIES.has(m.severity) &&
    typeof m.text === "string" &&
    m.text.length > 0 &&
    typeof m.timeUs === "number" &&
    (m.vehicleId === undefined || (typeof m.vehicleId === "number" && Number.isInteger(m.vehicleId)))
  );
}

/** §12 #5 schema: every §4 field present; numeric leaves number (airSpeed may be null). */
function telemetrySchemaOk(m: Record<string, unknown>): boolean {
  const num = (v: unknown) => typeof v === "number";
  const t = m as Record<string, Record<string, unknown>>;
  return (
    m.channel === "telemetry" &&
    m.vehicleId === 1 &&
    num(m.seq) &&
    typeof m.snapshot === "boolean" &&
    num(m.timeUs) &&
    num(t.attitude?.roll) && num(t.attitude?.pitch) && num(t.attitude?.yaw) &&
    num(t.position?.lat) && num(t.position?.lon) &&
    num(t.position?.altMSL) && num(t.position?.altRel) &&
    num(t.velocity?.groundSpeed) && num(t.velocity?.climbRate) &&
    (num(t.velocity?.airSpeed) || t.velocity?.airSpeed === null) &&
    num(t.battery?.percent) && num(t.battery?.voltage) && num(t.battery?.current) &&
    typeof t.gps?.fix === "string" && num(t.gps?.count) && num(t.gps?.hdop) &&
    typeof m.flightMode === "string" &&
    typeof m.armed === "boolean"
  );
}

async function main(): Promise<void> {
  // --- §12 #2: non-hello first message -> error AUTH_REQUIRED + close 1008.
  const unauth = await TestConn.open();
  unauth.send({ type: "subscribe", id: "x1", channel: "telemetry", vehicleId: 1 });
  const authErr = await unauth.next("AUTH_REQUIRED error", (m) => m.type === "error");
  check("#2 non-hello first message -> error AUTH_REQUIRED", authErr.code === "AUTH_REQUIRED", authErr);
  check("#2 socket closed with code 1008", (await unauth.waitClosed()) === 1008);

  // --- §12 #1/#2: hello -> helloAck on 127.0.0.1:8877.
  const conn = await TestConn.open();
  conn.send({ type: "hello", token: "dev-token", protocolVersion: "0.1", client: "selftest/0.1.0" });
  const helloAck = await conn.next("helloAck", (m) => m.type === "helloAck");
  check("#1/#2 hello -> helloAck (protocolVersion 0.1)", helloAck.protocolVersion === "0.1", helloAck);
  check(
    "#2 helloAck serverTimeUs is real wall-clock us",
    typeof helloAck.serverTimeUs === "number" &&
      Math.abs((helloAck.serverTimeUs as number) / 1000 - Date.now()) < 5000,
    helloAck.serverTimeUs,
  );

  // --- §14.3 welcome notification: "info" push right after helloAck, exact
  // text "Ghost bridge ready", no vehicleId.
  const infoNotif = await conn.next("welcome notification", (m) => m.type === "notification");
  check(
    "§14.3 welcome notification matches contract shape (info, exact text, no vehicleId)",
    notificationSchemaOk(infoNotif) &&
      infoNotif.severity === "info" &&
      infoNotif.text === "Ghost bridge ready" &&
      infoNotif.vehicleId === undefined,
    infoNotif,
  );

  // --- §12 #3: tick at 1 Hz with vehicleIds [1].
  const tick = await conn.next("tick", (m) => m.type === "tick", 1600);
  check(
    "#3 tick within 1.5 s with serverTimeUs + vehicleIds [1]",
    typeof tick.serverTimeUs === "number" &&
      typeof tick.uptimeS === "number" &&
      JSON.stringify(tick.vehicleIds) === "[1]",
    tick,
  );

  // --- §12 #4: subscribe -> subscribeAck + snapshot (seq 1, snapshot true).
  conn.send({ type: "subscribe", id: "s1", channel: "telemetry", vehicleId: 1 });
  const subAck = await conn.next("subscribeAck", (m) => m.type === "subscribeAck");
  check(
    "#4 subscribeAck echoes id/channel/vehicleId",
    subAck.id === "s1" && subAck.channel === "telemetry" && subAck.vehicleId === 1,
    subAck,
  );
  const snapshot = await conn.next("telemetry snapshot", isTelemetry);
  check("#4 snapshot has seq 1 + snapshot true", snapshot.seq === 1 && snapshot.snapshot === true, {
    seq: snapshot.seq,
    snapshot: snapshot.snapshot,
  });
  check("#4/#5 telemetry matches the full §4 schema", telemetrySchemaOk(snapshot), snapshot);

  // --- §12 #5: ~10 Hz stream, contiguous seq, varying values.
  const stream = await conn.collect(isTelemetry, 1050);
  check(`#5 ~10 Hz stream (got ${stream.length} msgs in 1.05 s)`, stream.length >= 8 && stream.length <= 13);
  check(
    "#5 seq contiguous from snapshot",
    stream.every((m, i) => m.seq === 2 + i && m.snapshot === false),
    stream.map((m) => m.seq),
  );
  check("#5 every stream message matches the §4 schema", stream.every(telemetrySchemaOk));
  const first = stream[0] as any;
  const last = stream[stream.length - 1] as any;
  check(
    "#5 values plausibly vary over 1 s",
    first.attitude.roll !== last.attitude.roll ||
      first.attitude.yaw !== last.attitude.yaw ||
      first.battery.percent !== last.battery.percent,
    { first: first.attitude, last: last.attitude },
  );

  // --- §12 #6: unsubscribe stops the stream; re-subscribe restarts seq at 1.
  conn.send({ type: "unsubscribe", id: "u1", channel: "telemetry", vehicleId: 1 });
  await conn.next("unsubscribeAck", (m) => m.type === "unsubscribeAck");
  await conn.collect(isTelemetry, 100); // drain anything in flight before the ack was processed
  const afterUnsub = await conn.collect(isTelemetry, 500);
  check("#6 unsubscribe stops the stream", afterUnsub.length === 0, afterUnsub.length);

  conn.send({ type: "subscribe", id: "s2", channel: "telemetry", vehicleId: 1 });
  await conn.next("re-subscribeAck", (m) => m.type === "subscribeAck" && m.id === "s2");
  const resnap = await conn.next("re-subscribe snapshot", isTelemetry);
  check("#6 re-subscribe restarts seq at 1 with a snapshot", resnap.seq === 1 && resnap.snapshot === true, {
    seq: resnap.seq,
    snapshot: resnap.snapshot,
  });

  // --- §12 #7: error codes; connection stays open throughout.
  const isErr = (id: string) => (m: Record<string, unknown>) => m.type === "error" && m.id === id;

  conn.send({ type: "subscribe", id: "e1", channel: "warpField", vehicleId: 1 });
  check("#7 unknown channel -> UNKNOWN_CHANNEL", (await conn.next("e1", isErr("e1"))).code === "UNKNOWN_CHANNEL");

  conn.send({ type: "subscribe", id: "e2", channel: "adsb" });
  check(
    "#7 out-of-scope M1 channel (adsb) -> UNKNOWN_CHANNEL",
    (await conn.next("e2", isErr("e2"))).code === "UNKNOWN_CHANNEL",
  );

  conn.send({ type: "subscribe", id: "e3", channel: "telemetry", vehicleId: 99 });
  check("#7 unknown vehicle -> UNKNOWN_VEHICLE", (await conn.next("e3", isErr("e3"))).code === "UNKNOWN_VEHICLE");

  conn.send({ type: "engageWarpDrive", id: "e4" });
  check("#7 unknown type -> UNKNOWN_TYPE", (await conn.next("e4", isErr("e4"))).code === "UNKNOWN_TYPE");

  conn.send("this is not json {");
  const badJson = await conn.next("BAD_MESSAGE", (m) => m.type === "error" && m.id === undefined);
  check("#7 unparseable JSON -> BAD_MESSAGE", badJson.code === "BAD_MESSAGE", badJson);

  conn.send({ type: "subscribe", id: "e6", channel: "telemetry" }); // missing vehicleId
  check("#7 missing required field -> BAD_MESSAGE", (await conn.next("e6", isErr("e6"))).code === "BAD_MESSAGE");

  // --- command lifecycle (W1a extra): accept-and-succeed ack keyed by id.
  conn.send({ type: "command", id: "c1", vehicleId: 1, action: "arm", params: {} });
  const cmdAck = await conn.next("commandAck", (m) => m.type === "commandAck" && m.id === "c1");
  check(
    "command -> commandAck accepted (mavResult 0)",
    cmdAck.status === "accepted" && cmdAck.mavResult === 0 && cmdAck.vehicleId === 1,
    cmdAck,
  );

  conn.send({ type: "command", id: "c2", vehicleId: 7, action: "arm", params: {} });
  check("command to unknown vehicle -> UNKNOWN_VEHICLE", (await conn.next("c2", isErr("c2"))).code === "UNKNOWN_VEHICLE");

  // --- #7 tail: the connection survived every error above (and still ticks).
  check("#7 connection stays open after errors", conn.isOpen);
  await conn.next("tick after errors", (m) => m.type === "tick", 1600);
  check("#3/#7 tick still arriving after error barrage", true);

  // --- command lifecycle (W3): arm/disarm flip telemetry.armed (the shared
  // vehicleOverride from server.ts). `conn` still has an active telemetry
  // subscription from the #6 re-subscribe above, which has been streaming
  // untouched (and unconsumed) in the background this whole time, so drain
  // the backlog before/after each ack: WS/TCP preserve per-connection
  // ordering, so anything the server sent before an ack is already buffered
  // by the time we receive that ack, and draining leaves only fresh,
  // post-command telemetry for the check that follows.
  await conn.collect(isTelemetry, 0);
  conn.send({ type: "command", id: "c3", vehicleId: 1, action: "disarm", params: {} });
  await conn.next("disarm commandAck", (m) => m.type === "commandAck" && m.id === "c3");
  await conn.collect(isTelemetry, 0);
  const disarmedTelemetry = await conn.next("telemetry after disarm", isTelemetry, 500);
  check("disarm flips telemetry.armed to false", disarmedTelemetry.armed === false, disarmedTelemetry.armed);

  conn.send({ type: "command", id: "c4", vehicleId: 1, action: "arm", params: {} });
  await conn.next("arm commandAck", (m) => m.type === "commandAck" && m.id === "c4");
  await conn.collect(isTelemetry, 0);
  const armedTelemetry = await conn.next("telemetry after arm", isTelemetry, 500);
  check("arm flips telemetry.armed to true", armedTelemetry.armed === true, armedTelemetry.armed);

  // --- command lifecycle (W3): takeoff -> accepted ack + progressive commandProgress (§5.3).
  conn.send({ type: "command", id: "c5", vehicleId: 1, action: "takeoff", params: { alt: 20 } });
  const takeoffAck = await conn.next("takeoff commandAck", (m) => m.type === "commandAck" && m.id === "c5");
  check("takeoff -> commandAck accepted", takeoffAck.status === "accepted" && takeoffAck.mavResult === 0, takeoffAck);
  const takeoffProgress = await conn.next(
    "takeoff commandProgress",
    (m) => m.type === "commandProgress" && m.id === "c5",
    4000,
  );
  check(
    "takeoff emits commandProgress with numeric progress + message",
    typeof takeoffProgress.progress === "number" &&
      takeoffProgress.progress > 0 &&
      takeoffProgress.progress <= 1 &&
      typeof takeoffProgress.message === "string" &&
      takeoffProgress.vehicleId === 1,
    takeoffProgress,
  );

  // --- takeoff with invalid params -> rejected ack, no progress.
  conn.send({ type: "command", id: "c6", vehicleId: 1, action: "takeoff", params: { alt: "high" } });
  const badTakeoffAck = await conn.next("bad takeoff commandAck", (m) => m.type === "commandAck" && m.id === "c6");
  check(
    "takeoff with non-numeric alt -> rejected with reason",
    badTakeoffAck.status === "rejected" && typeof badTakeoffAck.reason === "string",
    badTakeoffAck,
  );
  const noProgressAfterReject = await conn
    .collect((m) => m.type === "commandProgress" && m.id === "c6", 700)
    .catch(() => []);
  check("rejected takeoff emits no commandProgress", noProgressAfterReject.length === 0, noProgressAfterReject.length);

  // --- video channel (§9): subscribe -> videoConfig, then binary frames.
  conn.send({ type: "subscribe", id: "v1", channel: "video", streamId: 1 });
  const vSubAck = await conn.next("video subscribeAck", (m) => m.type === "subscribeAck" && m.id === "v1");
  check(
    "video subscribeAck echoes id/channel/streamId",
    vSubAck.channel === "video" && vSubAck.streamId === 1,
    vSubAck,
  );

  const videoConfig = await conn.next("videoConfig", (m) => m.type === "videoConfig");
  check(
    "videoConfig carries codec/dimensions/sps/pps (§9.1)",
    videoConfig.channel === "video" &&
      videoConfig.streamId === 1 &&
      videoConfig.snapshot === true &&
      videoConfig.codec === "h264" &&
      videoConfig.width === 640 &&
      videoConfig.height === 360 &&
      typeof videoConfig.sps === "string" &&
      videoConfig.sps.length > 0 &&
      typeof videoConfig.pps === "string" &&
      videoConfig.pps.length > 0,
    videoConfig,
  );

  const firstFrames = await conn.collectBinary(1, 1000);
  check("video: binary frame(s) arrive after videoConfig", firstFrames.length >= 1, firstFrames.length);
  const firstFrame = firstFrames[0]!;
  const dv0 = new DataView(firstFrame.buffer, firstFrame.byteOffset, firstFrame.byteLength);
  check("video: header magic is 0x4656", dv0.getUint16(0, true) === 0x4656, dv0.getUint16(0, true));
  check("video: header version is 1", dv0.getUint8(2) === 1, dv0.getUint8(2));
  check("video: header streamId matches subscribed id (1)", dv0.getUint8(3) === 1, dv0.getUint8(3));
  check("video: first frame is a keyframe (flags bit 0 set)", (dv0.getUint8(4) & 1) === 1, dv0.getUint8(4));
  check(
    "video: frame carries an Annex-B payload beyond the 16-byte header",
    firstFrame.length > 16,
    firstFrame.length,
  );

  // ~15 fps rate sanity: >=10 frames within a ~1 s window.
  await Bun.sleep(1000);
  const rateFrames = conn.binaryReceived.splice(0, conn.binaryReceived.length);
  check(
    `video: ~15 fps rate sanity (>=10 frames/s, got ${rateFrames.length})`,
    rateFrames.length >= 10,
    rateFrames.length,
  );

  conn.send({ type: "unsubscribe", id: "v2", channel: "video", streamId: 1 });
  await conn.next("video unsubscribeAck", (m) => m.type === "unsubscribeAck" && m.id === "v2");
  const afterVideoUnsub = await conn.collectBinary(1, 600);
  check("video: unsubscribe stops the binary stream", afterVideoUnsub.length === 0, afterVideoUnsub.length);

  // --- video channel (W5): a second stream (streamId 2) is subscribable
  // independently and tags its binary frames with its own streamId.
  conn.send({ type: "subscribe", id: "v3", channel: "video", streamId: 2 });
  const vSubAck2 = await conn.next("video subscribeAck (stream 2)", (m) => m.type === "subscribeAck" && m.id === "v3");
  check(
    "video subscribeAck (stream 2) echoes id/channel/streamId",
    vSubAck2.channel === "video" && vSubAck2.streamId === 2,
    vSubAck2,
  );

  const videoConfig2 = await conn.next("videoConfig (stream 2)", (m) => m.type === "videoConfig" && m.streamId === 2);
  check(
    "videoConfig (stream 2) carries codec/dimensions/sps/pps (§9.1)",
    videoConfig2.codec === "h264" && videoConfig2.width === 640 && videoConfig2.height === 360,
    videoConfig2,
  );

  const stream2Frames = await conn.collectBinary(1, 1000);
  check("video (stream 2): binary frame(s) arrive after videoConfig", stream2Frames.length >= 1, stream2Frames.length);
  const stream2Frame = stream2Frames[0]!;
  const dv2 = new DataView(stream2Frame.buffer, stream2Frame.byteOffset, stream2Frame.byteLength);
  check("video (stream 2): header streamId is 2", dv2.getUint8(3) === 2, dv2.getUint8(3));
  check("video (stream 2): first frame is a keyframe (flags bit 0 set)", (dv2.getUint8(4) & 1) === 1, dv2.getUint8(4));

  conn.send({ type: "unsubscribe", id: "v4", channel: "video", streamId: 2 });
  await conn.next("video unsubscribeAck (stream 2)", (m) => m.type === "unsubscribeAck" && m.id === "v4");

  // --- mission channel (PROTOCOL.md §7): subscribe -> snapshot with the
  // initial served mission — default (takeoff + 5 waypoints + RTL, seq 0..6,
  // 7 items) or, in fixture mode, the survey mission (takeoff + 4 survey
  // waypoints + RTL, seq 0..5, 6 items).
  const isMissionState = (m: Record<string, unknown>) => m.type === "missionState";
  const expectedInitialItemCount = FIXTURE_MODE ? 6 : 7;
  conn.send({ type: "subscribe", id: "mi1", channel: "mission", vehicleId: 1 });
  const missionSubAck = await conn.next("mission subscribeAck", (m) => m.type === "subscribeAck" && m.id === "mi1");
  check(
    "mission subscribeAck echoes id/channel/vehicleId",
    missionSubAck.channel === "mission" && missionSubAck.vehicleId === 1,
    missionSubAck,
  );
  const missionSnapshot = await conn.next("mission snapshot", isMissionState);
  const missionItems0 = missionSnapshot.items as Record<string, unknown>[];
  check(
    `mission snapshot has seq 1 + snapshot true + ${expectedInitialItemCount} fixture items`,
    missionSnapshot.seq === 1 &&
      missionSnapshot.snapshot === true &&
      missionItems0.length === expectedInitialItemCount,
    { seq: missionSnapshot.seq, snapshot: missionSnapshot.snapshot, itemCount: missionItems0.length },
  );
  check(
    "mission snapshot items match the §7.1 schema",
    missionItems0.every(
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
    missionItems0,
  );

  // --- missionUpload: valid 2-item mission -> ack accepted itemCount 2,
  // then the mission stream sees a missionState update reflecting it.
  const uploadedItems = [
    { seq: 0, frame: 6, command: 16, current: true, autoContinue: true, param1: 0, param2: 0, param3: 0, param4: 0, lat: 13.74, lon: 100.53, alt: 25 },
    { seq: 1, frame: 6, command: 20, current: false, autoContinue: true, param1: 0, param2: 0, param3: 0, param4: 0, lat: 0, lon: 0, alt: 0 },
  ];
  conn.send({ type: "missionUpload", id: "mu1", vehicleId: 1, items: uploadedItems });
  const uploadAck = await conn.next("missionUpload ack", (m) => m.type === "missionAck" && m.id === "mu1");
  check(
    "missionUpload -> missionAck accepted itemCount 2",
    uploadAck.status === "accepted" && uploadAck.itemCount === 2,
    uploadAck,
  );
  const afterUpload = await conn.next("missionState update after upload", isMissionState, 2000);
  const afterUploadItems = afterUpload.items as Record<string, unknown>[];
  check(
    "missionState update after upload reflects the new 2-item mission",
    afterUploadItems.length === 2 && afterUploadItems[0]?.command === 16 && afterUploadItems[1]?.command === 20,
    afterUploadItems,
  );

  // --- missionClear: ack accepted itemCount 0, then the stream reflects an empty mission.
  conn.send({ type: "missionClear", id: "mc1", vehicleId: 1 });
  const clearAck = await conn.next("missionClear ack", (m) => m.type === "missionAck" && m.id === "mc1");
  check("missionClear -> missionAck accepted itemCount 0", clearAck.status === "accepted" && clearAck.itemCount === 0, clearAck);
  const afterClear = await conn.next("missionState update after clear", isMissionState, 2000);
  check(
    "missionState update after clear has an empty item list",
    Array.isArray(afterClear.items) && (afterClear.items as unknown[]).length === 0,
    afterClear.items,
  );

  // --- missionUpload: malformed items (missing required fields) -> rejected with a reason.
  conn.send({
    type: "missionUpload",
    id: "mu2",
    vehicleId: 1,
    items: [{ seq: 0, frame: 6, command: 16 /* missing current/autoContinue/param1-4/lat/lon/alt */ }],
  });
  const badUploadAck = await conn.next("bad missionUpload ack", (m) => m.type === "missionAck" && m.id === "mu2");
  check(
    "missionUpload with malformed items -> rejected with reason",
    badUploadAck.status === "rejected" && typeof badUploadAck.reason === "string",
    badUploadAck,
  );

  conn.send({ type: "unsubscribe", id: "mi2", channel: "mission", vehicleId: 1 });
  await conn.next("mission unsubscribeAck", (m) => m.type === "unsubscribeAck" && m.id === "mi2");

  // --- notification demo (mock-only, not §14 sourcing — see server.ts):
  // warning then critical push, vehicle-scoped (§14's optional vehicleId).
  // The server is spawned below with MOCK_NOTIFICATION_*_MS shortened so
  // this doesn't wait the real 15s/40s dev timing.
  const warningNotif = await conn.next(
    "warning notification",
    (m) => m.type === "notification" && m.severity === "warning",
    5000,
  );
  check(
    "notification: warning push matches contract shape (vehicleId 1)",
    notificationSchemaOk(warningNotif) && warningNotif.vehicleId === 1,
    warningNotif,
  );

  const criticalNotif = await conn.next(
    "critical notification",
    (m) => m.type === "notification" && m.severity === "critical",
    5000,
  );
  check(
    "notification: critical push matches contract shape (vehicleId 1)",
    notificationSchemaOk(criticalNotif) && criticalNotif.vehicleId === 1,
    criticalNotif,
  );

  conn.close();
}

// Spawn the server under test, run the checks, then tear it down.
// MOCK_NOTIFICATION_*_MS shortened so the notification-demo checks above
// don't wait the real 15s/40s dev timing (see server.ts's "notification demo").
const serverProc = Bun.spawn(["bun", new URL("./server.ts", import.meta.url).pathname], {
  stdout: "pipe",
  stderr: "inherit",
  env: {
    ...process.env,
    MOCK_NOTIFICATION_WARNING_MS: "50",
    MOCK_NOTIFICATION_CRITICAL_MS: "150",
  },
});
try {
  // Wait for the listen line before connecting.
  const reader = serverProc.stdout.getReader();
  await reader.read();
  reader.releaseLock();

  await main();
} catch (error) {
  failed += 1;
  console.error("FAIL (aborted)", error);
} finally {
  serverProc.kill();
}

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
