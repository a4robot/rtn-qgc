/**
 * §1 transport / §1.1 hello-auth / §14.3 welcome notification.
 *
 * Covers: WS upgrade succeeds, the "first message must be hello" rule (AUTH_REQUIRED + close
 * 1008), successful hello -> helloAck (protocolVersion echo, serverVersion, plausible
 * serverTimeUs), the welcome notification that immediately follows, UNSUPPORTED_VERSION on a
 * mismatched major version, and two bonus/informative checks (empty token -> BAD_MESSAGE,
 * re-hello on an already-authed connection is accepted as an idempotent re-ack) that pin real,
 * observed behavior even though PROTOCOL.md does not explicitly mandate either.
 */
import { check, notificationSchemaOk, TestConn } from "../lib.ts";

export async function runConnectionGroup(url: string): Promise<void> {
  // --- WS upgrade / connect succeeds at all.
  const unauth = await TestConn.open(url);
  check("connection: WS upgrade succeeds", unauth.isOpen);

  // --- §1.1: "The first message on a connection MUST be hello. Any other first message -> error
  // AUTH_REQUIRED and the server closes the socket (WS close code 1008)."
  unauth.send({ type: "subscribe", id: "x1", channel: "telemetry", vehicleId: 1 });
  const authErr = await unauth.next("AUTH_REQUIRED error", (m) => m.type === "error");
  check("§1.1 non-hello first message -> error AUTH_REQUIRED", authErr.code === "AUTH_REQUIRED", authErr);
  check("§1.1 socket closed with WS close code 1008", (await unauth.waitClosed()) === 1008);

  // --- §1.1: hello -> helloAck, protocolVersion echoed, serverVersion present, serverTimeUs is
  // real wall-clock microseconds (within a generous 5s skew budget).
  const conn = await TestConn.open(url);
  conn.send({ type: "hello", id: "h1", token: "dev-token", protocolVersion: "0.1", client: "conformance-suite/0.1.0" });
  const helloAck = await conn.next("helloAck", (m) => m.type === "helloAck");
  check("§1.1 hello -> helloAck echoes protocolVersion 0.1", helloAck.protocolVersion === "0.1", helloAck);
  check("§1.1 helloAck carries a non-empty serverVersion string", typeof helloAck.serverVersion === "string" && (helloAck.serverVersion as string).length > 0, helloAck.serverVersion);
  check(
    "§1.1 helloAck.serverTimeUs is real wall-clock microseconds",
    typeof helloAck.serverTimeUs === "number" && Math.abs((helloAck.serverTimeUs as number) / 1000 - Date.now()) < 5000,
    helloAck.serverTimeUs,
  );

  // --- §14.3: "Immediately after helloAck, the server sends that client -- only that client --
  // one notification with severity info, text 'Ghost bridge ready', no vehicleId."
  const welcome = await conn.next("welcome notification", (m) => m.type === "notification", 1500);
  check(
    "§14.3 welcome notification: shape + exact text + no vehicleId",
    notificationSchemaOk(welcome) && welcome.severity === "info" && welcome.text === "Ghost bridge ready" && welcome.vehicleId === undefined,
    welcome,
  );

  // --- Bonus/informative: re-hello on an already-authenticated connection is accepted as an
  // idempotent re-ack (WebBridgeServer.cc: "the protocol does not forbid it"). Not normative
  // text in PROTOCOL.md either way -- pinned here as documented real behavior, not a gap.
  conn.send({ type: "hello", id: "h2", token: "dev-token", protocolVersion: "0.1" });
  const reHelloAck = await conn.next("re-hello helloAck", (m) => m.type === "helloAck" && m.id === undefined, 1500).catch(() => null);
  // helloAck never carries an id field in this implementation (hello has no id echo in the
  // wire example either) -- just confirm a second helloAck arrives at all.
  const reHello2 = reHelloAck ?? (await conn.tryNext((m) => m.type === "helloAck", 500));
  check("bonus: re-hello on authed connection re-acks instead of erroring", reHello2 !== null, reHello2);

  conn.close();

  // --- §1.1: "If protocolVersion major version is unsupported, server replies with error
  // UNSUPPORTED_VERSION and closes."
  const badVersion = await TestConn.open(url);
  badVersion.send({ type: "hello", id: "v1", token: "dev-token", protocolVersion: "9.0" });
  const versionErr = await badVersion.next("UNSUPPORTED_VERSION error", (m) => m.type === "error");
  check("§1.1 unsupported major protocolVersion -> error UNSUPPORTED_VERSION", versionErr.code === "UNSUPPORTED_VERSION", versionErr);
  const versionCloseCode = await badVersion.waitClosed();
  check("§1.1 connection closes after UNSUPPORTED_VERSION (observed WS close code 1002)", versionCloseCode !== null && versionCloseCode !== undefined, versionCloseCode);

  // --- Bonus/informative: hello with an empty token is rejected as BAD_MESSAGE (consistent with
  // §1.1's "the field is required"). Not a literal quote from PROTOCOL.md, but a direct
  // consequence of it; pinned as real behavior.
  const emptyToken = await TestConn.open(url);
  emptyToken.send({ type: "hello", id: "e1", token: "", protocolVersion: "0.1" });
  const emptyTokenErr = await emptyToken.next("empty token error", (m) => m.type === "error");
  check("bonus: hello with empty token -> BAD_MESSAGE", emptyTokenErr.code === "BAD_MESSAGE", emptyTokenErr);
  emptyToken.close();
}
