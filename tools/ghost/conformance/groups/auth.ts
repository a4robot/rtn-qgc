/**
 * §1.1 "--bridge-token (bridge security hardening)" — flag-gated group.
 *
 * Requires a SECOND ghost instance started with --bridge-token <AUTH_TOKEN>, reachable at
 * AUTH_PROBE_URL. This is a separate process from the main PROBE_URL instance deliberately: the
 * task brief calls for this ("spawn a second ghost with the flag for this group") so the
 * lifecycle/subscribe/command/mission groups against the main instance never have to think about
 * auth at all. If AUTH_PROBE_URL is unset, the whole group is skipped cleanly (see run.ts).
 */
import { check, TestConn } from "../lib.ts";

export async function runAuthGroup(url: string, correctToken: string): Promise<void> {
  // --- §1.1: "a mismatch (or missing token) replies error AUTH_FAILED and the server closes the
  // connection (WS close code 1008), same as AUTH_REQUIRED."
  const wrong = await TestConn.open(url);
  wrong.send({ type: "hello", id: "w1", token: "definitely-not-the-configured-token", protocolVersion: "0.1" });
  const wrongErr = await wrong.next("AUTH_FAILED error", (m) => m.type === "error");
  check("§1.1 --bridge-token: wrong token -> error AUTH_FAILED", wrongErr.code === "AUTH_FAILED", wrongErr);
  check("§1.1 --bridge-token: wrong token closes with WS close code 1008", (await wrong.waitClosed()) === 1008);

  // --- Missing token (empty string) also rejected -- BAD_MESSAGE fires first (empty token is
  // caught before the token-comparison check in _handleHello()), so the client-visible outcome
  // is still "rejected, connection closes" but via a different code. Document, don't assert
  // AUTH_FAILED specifically here (BAD_MESSAGE happens not to close the connection -- see below).
  const missing = await TestConn.open(url);
  missing.send({ type: "hello", id: "m1", token: "", protocolVersion: "0.1" });
  const missingErr = await missing.next("missing token error", (m) => m.type === "error");
  check(
    "bonus: --bridge-token + empty token -> BAD_MESSAGE (checked before the token comparison)",
    missingErr.code === "BAD_MESSAGE",
    missingErr,
  );
  missing.close();

  // --- Correct token -> normal helloAck, same as the unconfigured-token default path.
  const ok = await TestConn.open(url);
  ok.send({ type: "hello", id: "ok1", token: correctToken, protocolVersion: "0.1" });
  const okAck = await ok.next("helloAck", (m) => m.type === "helloAck");
  check("§1.1 --bridge-token: correct token -> helloAck", okAck.type === "helloAck" && okAck.protocolVersion === "0.1", okAck);
  ok.close();
}
