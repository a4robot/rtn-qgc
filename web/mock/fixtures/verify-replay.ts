/**
 * Standalone checker for the mock server's opt-in fixture mode
 * (`MOCK_FIXTURE=1`): spawns `server.ts` with the flag set, connects with a
 * plain WebSocket, and verifies telemetry replay is §4-shaped (and actually
 * comes from the Zurich survey fixture, not the default Bangkok orbit) plus
 * the initial served mission is the 6-item survey mission (§7.1).
 *
 * Runs in its own OS process (spawned by mock/fixtures/survey.test.ts via
 * Bun.spawn) specifically so it gets a pristine `WebSocket` global —
 * src/bridge/BridgeClient.test.ts monkeypatches `globalThis.WebSocket` in a
 * `beforeEach` with no matching restore, which otherwise leaks into every
 * other in-process `bun test` file that runs after it.
 *
 * Prints one `RESULT: PASS <name>` / `RESULT: FAIL <name> <detail>` line per
 * check; exits 0 iff every check passed.
 *
 * Run: bun mock/fixtures/verify-replay.ts
 */

const port = Number(process.env.MOCK_PORT ?? 8879);

let failed = false;
function check(name: string, cond: boolean, detail?: unknown): void {
  if (cond) {
    console.log(`RESULT: PASS ${name}`);
  } else {
    failed = true;
    console.log(`RESULT: FAIL ${name} ${detail === undefined ? "" : JSON.stringify(detail)}`);
  }
}

async function main(): Promise<void> {
  const socket = new WebSocket(`ws://127.0.0.1:${port}/`);
  const received: Record<string, unknown>[] = [];
  socket.onmessage = (event: MessageEvent) => {
    if (typeof event.data === "string") received.push(JSON.parse(event.data) as Record<string, unknown>);
  };
  await new Promise<void>((resolve, reject) => {
    socket.onopen = () => resolve();
    socket.onerror = () => reject(new Error("cannot connect"));
  });
  socket.send(JSON.stringify({ type: "hello", token: "dev-token", protocolVersion: "0.1" }));
  socket.send(JSON.stringify({ type: "subscribe", id: "t1", channel: "telemetry", vehicleId: 1 }));
  socket.send(JSON.stringify({ type: "subscribe", id: "m1", channel: "mission", vehicleId: 1 }));

  const deadline = Date.now() + 5000;
  while (
    (received.filter((m) => m.type === "telemetry").length < 3 || !received.some((m) => m.type === "missionState")) &&
    Date.now() < deadline
  ) {
    await Bun.sleep(20);
  }

  const telemetry = received.filter((m) => m.type === "telemetry");
  check("telemetry replay produces >=3 messages", telemetry.length >= 3, telemetry.length);

  const isZurich = (m: Record<string, unknown>) => {
    const position = m.position as { lat: number; lon: number } | undefined;
    return !!position && position.lat > 47.3 && position.lat < 47.5 && position.lon > 8.4 && position.lon < 8.6;
  };
  check(
    "every telemetry message is §4-shaped and near Zurich (survey fixture, not the default Bangkok orbit)",
    telemetry.length > 0 &&
      telemetry.every(
        (m) =>
          m.channel === "telemetry" &&
          m.vehicleId === 1 &&
          typeof m.seq === "number" &&
          typeof m.snapshot === "boolean" &&
          typeof m.armed === "boolean" &&
          typeof m.flightMode === "string" &&
          isZurich(m),
      ),
    telemetry[0],
  );

  const missionState = received.find((m) => m.type === "missionState");
  check(
    "initial served mission is the 6-item survey mission",
    !!missionState && Array.isArray(missionState.items) && (missionState.items as unknown[]).length === 6,
    missionState?.items,
  );

  socket.close();
}

const serverProc = Bun.spawn(["bun", new URL("../server.ts", import.meta.url).pathname], {
  env: { ...process.env, MOCK_FIXTURE: "1", MOCK_PORT: String(port) },
  stdout: "pipe",
  stderr: "inherit",
});
try {
  const reader = serverProc.stdout!.getReader();
  await reader.read(); // wait for the "listening on" line
  reader.releaseLock();
  await main();
} catch (error) {
  failed = true;
  console.log(`RESULT: FAIL (aborted) ${String(error)}`);
} finally {
  serverProc.kill();
}

process.exit(failed ? 1 : 0);
