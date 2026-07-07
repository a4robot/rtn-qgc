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

  conn.close();
}

// Spawn the server under test, run the checks, then tear it down.
const serverProc = Bun.spawn(["bun", new URL("./server.ts", import.meta.url).pathname], {
  stdout: "pipe",
  stderr: "inherit",
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
