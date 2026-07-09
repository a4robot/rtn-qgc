/**
 * Shared harness for the PROTOCOL.md conformance suite (tools/ghost/conformance/).
 *
 * This suite runs against the REAL C++ ghost bridge (src/WebBridge/), not the web/mock/ server —
 * see run.ts's header comment for the full rationale (Wave 14 / M7 job Q7e prep). Modeled on
 * web/mock/selftest.ts's TestConn + check() pattern, extended with two more outcome kinds:
 *
 *   - check(): a hard PASS/FAIL assertion of NORMATIVE behavior (PROTOCOL.md text). A FAIL here
 *     means either the suite misread the spec (fix the suite) or the real bridge has a genuine
 *     conformance gap (fix the bridge — NOT this wave; see run.ts's findings list). FAILs make the
 *     process exit non-zero.
 *   - gap(): asserts the bridge's ACTUAL observed behavior where it is known, at the time this
 *     suite was written, to diverge from a normative PROTOCOL.md statement. Printed as "GAP", not
 *     "FAIL" or "PASS", and does NOT affect the exit code -- it exists so the divergence is
 *     characterized (pinned, like any characterization test) rather than either silently passing
 *     as if it were correct or permanently failing CI. Every gap() call carries a comment citing
 *     the PROTOCOL.md section it diverges from. If a future wave fixes the bridge to match spec,
 *     this call starts asserting the OLD (now wrong) behavior and will itself fail loudly --
 *     that's the intended signal to promote it back to a plain check().
 *   - skip(): environment-gated behavior that could not be exercised this run (e.g. no video
 *     frames arrived within the timeout). Does not affect the exit code.
 */

export interface Results {
  passed: number;
  failed: number;
  gaps: number;
  skipped: number;
  failNames: string[];
  gapNames: string[];
  skipNames: string[];
}

export const results: Results = { passed: 0, failed: 0, gaps: 0, skipped: 0, failNames: [], gapNames: [], skipNames: [] };

export function check(name: string, cond: boolean, detail?: unknown): void {
  if (cond) {
    results.passed += 1;
    console.log(`PASS ${name}`);
  } else {
    results.failed += 1;
    results.failNames.push(name);
    console.error(`FAIL ${name}`, detail === undefined ? "" : JSON.stringify(detail));
  }
}

/** See the class doc comment above: pins a known doc/implementation divergence. `cond` should be
 * the assertion of the bridge's ACTUAL behavior (so this reads GAP, not FAIL, today). */
export function gap(name: string, cond: boolean, detail?: unknown): void {
  results.gaps += 1;
  results.gapNames.push(name);
  const tag = cond ? "GAP " : "GAP!";
  console.warn(`${tag} ${name}`, detail === undefined ? "" : JSON.stringify(detail));
  if (!cond) {
    // The bridge's behavior no longer matches what we characterized -- something changed
    // (possibly a fix, possibly a regression). Either way this needs human eyes, so escalate to a
    // real failure rather than silently staying a GAP.
    results.failed += 1;
    results.failNames.push(`${name} (gap characterization stale -- behavior changed)`);
  }
}

export function skip(name: string, reason: string): void {
  results.skipped += 1;
  results.skipNames.push(name);
  console.log(`SKIP ${name} -- ${reason}`);
}

export function section(title: string): void {
  console.log(`\n== ${title} ==`);
}

/** Minimal test connection: buffers parsed messages, supports predicate waits. Mirrors
 * web/mock/selftest.ts's TestConn (kept in sync deliberately -- same ergonomics, same bugs-found
 * class, different server under test). */
export class TestConn {
  readonly received: Record<string, unknown>[] = [];
  /** Raw binary WS frames (video, §9.2), in arrival order. */
  readonly binaryReceived: Uint8Array[] = [];
  closeCode: number | null = null;
  closeReason: string | null = null;
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
      this.closeReason = event.reason;
    };
  }

  static async open(url: string): Promise<TestConn> {
    const socket = new WebSocket(url);
    await new Promise<void>((resolve, reject) => {
      socket.onopen = () => resolve();
      socket.onerror = () => reject(new Error(`cannot connect to ${url}`));
    });
    return new TestConn(socket);
  }

  /** hello + wait for helloAck, in one call -- the common case for every group after "connection". */
  static async openAuthed(url: string, token = "dev-token", protocolVersion = "0.1"): Promise<TestConn> {
    const conn = await TestConn.open(url);
    conn.send({ type: "hello", id: "hello-1", token, protocolVersion, client: "conformance-suite/0.1.0" });
    await conn.next("helloAck", (m) => m.type === "helloAck");
    return conn;
  }

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
      await Bun.sleep(15);
    }
    throw new Error(`timeout waiting for ${label}`);
  }

  /** Like next(), but returns null instead of throwing on timeout -- for "assert absence". */
  async tryNext(
    pred: (m: Record<string, unknown>) => boolean,
    timeoutMs: number,
  ): Promise<Record<string, unknown> | null> {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      const i = this.received.findIndex(pred);
      if (i >= 0) {
        return this.received.splice(i, 1)[0]!;
      }
      await Bun.sleep(15);
    }
    return null;
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
      await Bun.sleep(15);
    }
    throw new Error("timeout waiting for close");
  }

  close(): void {
    try {
      this.socket.close();
    } catch {
      // already closed -- fine
    }
  }
}

export const NOTIFICATION_SEVERITIES = new Set(["info", "warning", "critical"]);

/** Shape check for the §14 notification push contract: {type, severity, text, timeUs, vehicleId?}. */
export function notificationSchemaOk(m: Record<string, unknown>): boolean {
  return (
    m.type === "notification" &&
    typeof m.severity === "string" &&
    NOTIFICATION_SEVERITIES.has(m.severity as string) &&
    typeof m.text === "string" &&
    (m.text as string).length > 0 &&
    typeof m.timeUs === "number" &&
    (m.vehicleId === undefined || (typeof m.vehicleId === "number" && Number.isInteger(m.vehicleId)))
  );
}

/** §4 schema: every field present; numeric leaves are number|null per the "nullable discipline"
 * (fields whose value is unknown are null, never omitted -- PROTOCOL.md §4 lead paragraph). */
export function telemetrySchemaOk(m: Record<string, unknown>, vehicleId: number): { ok: boolean; reason?: string } {
  const numOrNull = (v: unknown) => typeof v === "number" || v === null;
  const t = m as Record<string, Record<string, unknown>>;
  const checks: [string, boolean][] = [
    ["channel", m.channel === "telemetry"],
    ["vehicleId", m.vehicleId === vehicleId],
    ["seq", typeof m.seq === "number"],
    ["snapshot", typeof m.snapshot === "boolean"],
    ["timeUs", typeof m.timeUs === "number"],
    ["attitude present", typeof t.attitude === "object" && t.attitude !== null],
    ["attitude.roll", numOrNull(t.attitude?.roll)],
    ["attitude.pitch", numOrNull(t.attitude?.pitch)],
    ["attitude.yaw", numOrNull(t.attitude?.yaw)],
    ["position.lat", numOrNull(t.position?.lat)],
    ["position.lon", numOrNull(t.position?.lon)],
    ["position.altMSL", numOrNull(t.position?.altMSL)],
    ["position.altRel", numOrNull(t.position?.altRel)],
    ["velocity.groundSpeed", numOrNull(t.velocity?.groundSpeed)],
    ["velocity.airSpeed", numOrNull(t.velocity?.airSpeed)],
    ["velocity.climbRate", numOrNull(t.velocity?.climbRate)],
    ["battery.percent", numOrNull(t.battery?.percent)],
    ["battery.voltage", numOrNull(t.battery?.voltage)],
    ["battery.current", numOrNull(t.battery?.current)],
    ["gps.fix", typeof t.gps?.fix === "string" && ["none", "2d", "3d", "rtkFloat", "rtkFixed"].includes(t.gps.fix as string)],
    ["gps.count", numOrNull(t.gps?.count)],
    ["gps.hdop", numOrNull(t.gps?.hdop)],
    ["flightMode", typeof m.flightMode === "string"],
    ["armed", typeof m.armed === "boolean"],
  ];
  const bad = checks.find(([, ok]) => !ok);
  return bad ? { ok: false, reason: bad[0] } : { ok: true };
}
