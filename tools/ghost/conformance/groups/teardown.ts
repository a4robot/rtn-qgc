/**
 * Teardown/robustness: the server survives an abrupt client disconnect (no clean unsubscribe,
 * no explicit close handshake beyond what the WS client API itself sends) and keeps serving new
 * connections normally afterward.
 *
 * This is the achievable approximation of "abrupt close" from a Bun WS client: a bare socket
 * .close() with no prior unsubscribe/goodbye message, which still exercises
 * WebBridgeServer::_onDisconnected()'s cleanup path (_clients/_tokenToClient bookkeeping) the
 * same way a tab close or network drop would.
 */
import { check, TestConn } from "../lib.ts";

export async function runTeardownGroup(url: string, vehicleId: number): Promise<void> {
  const doomed = await TestConn.openAuthed(url);
  doomed.send({ type: "subscribe", id: "d1", channel: "telemetry", vehicleId });
  await doomed.next("doomed subscribeAck", (m) => m.type === "subscribeAck" && m.id === "d1");
  await doomed.next("doomed snapshot", (m) => m.type === "telemetry" && m.vehicleId === vehicleId);

  // Abrupt close: no unsubscribe, no goodbye -- just gone, mid-stream.
  doomed.close();
  await Bun.sleep(300);

  // The server must still be alive and answer a fresh connection normally.
  const survivor = await TestConn.open(url);
  check("teardown: server still accepts new connections after an abrupt client close", survivor.isOpen);
  survivor.send({ type: "hello", id: "s1", token: "dev-token", protocolVersion: "0.1" });
  const helloAck = await survivor.next("survivor helloAck", (m) => m.type === "helloAck", 2000);
  check("teardown: server still completes hello/helloAck normally after the abrupt close", helloAck.type === "helloAck", helloAck);

  // And the vehicle/telemetry channel still works end-to-end (proves no shared state got wedged).
  survivor.send({ type: "subscribe", id: "s2", channel: "telemetry", vehicleId });
  await survivor.next("survivor subscribeAck", (m) => m.type === "subscribeAck" && m.id === "s2");
  const snapshot = await survivor.next("survivor snapshot", (m) => m.type === "telemetry" && m.vehicleId === vehicleId, 2000);
  check("teardown: telemetry channel still functions for a new client after the abrupt close", snapshot.seq === 1 && snapshot.snapshot === true, snapshot);

  survivor.close();
}
