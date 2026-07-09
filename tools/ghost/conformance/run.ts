#!/usr/bin/env bun
/**
 * PROTOCOL.md conformance suite — runs against the REAL C++ ghost bridge (src/WebBridge/), not
 * web/mock/server.ts.
 *
 * Why this exists (Wave 14 / M7 prep, job Q7e): STRANGLER_MILESTONES.md's testing protocol (the
 * "de-Qt testing protocol" section) requires a protocol conformance suite against the real bridge
 * BEFORE QWebSocketServer/QWebSocket get swapped out for a non-Qt WS library. This suite is that
 * characterization baseline: it walks PROTOCOL.md §1-§14 end to end against a live ghost process
 * (`--headless --bridge-port <port> --mock-link`) and pins exactly what the current
 * implementation does, including the handful of places it diverges from the letter of the spec
 * (see the gap() calls throughout groups/*.ts) — those divergences are the contract the WS-lib
 * swap must either reproduce byte-for-byte or a later wave must fix in the C++ FIRST, not
 * silently change as a side effect of the swap.
 *
 * Relationship to other test assets:
 *   - web/mock/selftest.ts tests the MOCK server (web/mock/server.ts) against the same protocol
 *     text, so a web client can develop against the mock with confidence. This suite tests the
 *     real bridge instead — same spec, different implementation under test, so PASS/FAIL here can
 *     legitimately differ from selftest.ts for the same nominal behavior (see gap() findings).
 *   - tools/ghost/mockghostprobe.ts is CI's narrow "does the flagship arm/telemetry/mission flow
 *     still work" smoke probe. This suite is broader (every §1-§14 behavior a WS-library swap
 *     could break) and is a superset in spirit, not a replacement — mockghostprobe.ts stays as
 *     the fast CI gate; this suite is the pre-swap characterization pass (and can be re-run
 *     post-swap as an equivalence check, per the testing protocol's "equivalence phase").
 *
 * Usage:
 *   PROBE_URL=ws://127.0.0.1:8875 bun tools/ghost/conformance/run.ts
 *
 * Env vars:
 *   PROBE_URL        Main ghost instance (hello/subscribe/telemetry/command/mission/tick/
 *                     multi-client/teardown groups all run against this one). Default
 *                     ws://127.0.0.1:8877 (PROTOCOL.md §1's documented default port) — almost
 *                     always overridden in practice, exactly like mockghostprobe.ts's PROBE_URL.
 *   VEHICLE_ID        MockLink vehicle system id to exercise. Default 128 (MockLink's PX4
 *                     system id, per mockghostprobe.ts).
 *   AUTH_PROBE_URL    A SECOND ghost instance started with --bridge-token <AUTH_TOKEN>. If unset,
 *                     the auth group is SKIPPED cleanly (not a failure) — the task brief allows
 *                     either spawning a second ghost or documenting the skip; this suite does the
 *                     former when the caller provides one, the latter otherwise.
 *   AUTH_TOKEN        The token the AUTH_PROBE_URL ghost was started with. Required if
 *                     AUTH_PROBE_URL is set.
 *   VIDEO_PROBE_URL   Ghost instance to use for the video group (may be a separate, already-
 *                     running, read-only instance with a live GStreamer feed — see the task
 *                     brief). Defaults to PROBE_URL.
 *   VIDEO_STREAM_ID   §9 streamId to subscribe. Default 1.
 *   VIDEO_EXPECTED    "1" to make the video group FAIL (not SKIP) when no frames arrive — for CI
 *                     legs that provision their own feed and want to catch a regression.
 *
 * Exit code: 0 if every check() passed (gap()s and skip()s do not affect this), 1 otherwise.
 */
import { results, section } from "./lib.ts";
import { runAuthGroup } from "./groups/auth.ts";
import { runCommandGroup } from "./groups/command.ts";
import { runConnectionGroup } from "./groups/connection.ts";
import { runImageGroup } from "./groups/image.ts";
import { runMissionGroup } from "./groups/mission.ts";
import { runMultiClientGroup } from "./groups/multiclient.ts";
import { runSubscribeGroup } from "./groups/subscribe.ts";
import { runTeardownGroup } from "./groups/teardown.ts";
import { runTelemetryGroup } from "./groups/telemetry.ts";
import { runTickGroup } from "./groups/tick.ts";
import { runVideoGroup } from "./groups/video.ts";

const PROBE_URL = process.env.PROBE_URL ?? "ws://127.0.0.1:8877";
const VEHICLE_ID = Number(process.env.VEHICLE_ID ?? "128");
const AUTH_PROBE_URL = process.env.AUTH_PROBE_URL;
const AUTH_TOKEN = process.env.AUTH_TOKEN;
const VIDEO_PROBE_URL = process.env.VIDEO_PROBE_URL ?? PROBE_URL;
const VIDEO_STREAM_ID = Number(process.env.VIDEO_STREAM_ID ?? "1");
const VIDEO_EXPECTED = process.env.VIDEO_EXPECTED === "1";

type Group = { name: string; run: () => Promise<void> };

async function runGroup(group: Group): Promise<void> {
  section(group.name);
  try {
    await group.run();
  } catch (error) {
    results.failed += 1;
    results.failNames.push(`${group.name} (group aborted)`);
    console.error(`FAIL ${group.name} (group aborted)`, error instanceof Error ? error.stack ?? error.message : error);
  }
}

async function main(): Promise<void> {
  console.log(`PROTOCOL.md conformance suite -- target ${PROBE_URL}, vehicleId ${VEHICLE_ID}`);

  const groups: Group[] = [
    { name: "§1/§1.1/§14.3 connection + hello/helloAck + welcome notification", run: () => runConnectionGroup(PROBE_URL) },
    { name: "§3.1/§2.2/§2.4/§10 subscribe/unsubscribe lifecycle + errors", run: () => runSubscribeGroup(PROBE_URL, VEHICLE_ID) },
    { name: "§4 telemetry shape/rate/nullable discipline", run: () => runTelemetryGroup(PROBE_URL, VEHICLE_ID) },
    { name: "§5 command request/ack correlation", run: () => runCommandGroup(PROBE_URL, VEHICLE_ID) },
    { name: "§7 mission upload/download/clear round-trip", run: () => runMissionGroup(PROBE_URL, VEHICLE_ID) },
    { name: "§15 image channel + byte-integrity (Q8d, requires --mock-link enableCamera)", run: () => runImageGroup(PROBE_URL, VEHICLE_ID) },
    { name: "§11.1 tick cadence + vehicleIds", run: () => runTickGroup(PROBE_URL, VEHICLE_ID) },
    { name: "multi-client isolation (§14.3 welcome, §11.1 broadcast, §3 scoping)", run: () => runMultiClientGroup(PROBE_URL, VEHICLE_ID) },
    { name: "teardown: server survives abrupt client close", run: () => runTeardownGroup(PROBE_URL, VEHICLE_ID) },
  ];

  for (const group of groups) {
    await runGroup(group);
  }

  if (AUTH_PROBE_URL) {
    if (!AUTH_TOKEN) {
      results.failed += 1;
      results.failNames.push("auth group misconfigured");
      console.error("FAIL auth group misconfigured -- AUTH_PROBE_URL is set but AUTH_TOKEN is not");
    } else {
      await runGroup({ name: "§1.1 --bridge-token AUTH_FAILED/success", run: () => runAuthGroup(AUTH_PROBE_URL, AUTH_TOKEN) });
    }
  } else {
    section("§1.1 --bridge-token AUTH_FAILED/success");
    results.skipped += 1;
    results.skipNames.push("§1.1 --bridge-token group");
    console.log("SKIP §1.1 --bridge-token group -- AUTH_PROBE_URL not set (spawn a second ghost with --bridge-token and set AUTH_PROBE_URL/AUTH_TOKEN to run it)");
  }

  await runGroup({ name: `§9 video channel (streamId ${VIDEO_STREAM_ID}, target ${VIDEO_PROBE_URL})`, run: () => runVideoGroup(VIDEO_PROBE_URL, VIDEO_STREAM_ID, VIDEO_EXPECTED) });

  console.log("\n========================================");
  console.log(`PASS: ${results.passed}   FAIL: ${results.failed}   GAP: ${results.gaps}   SKIP: ${results.skipped}`);
  if (results.failNames.length > 0) {
    console.log("\nFAILED:");
    for (const name of results.failNames) console.log(`  - ${name}`);
  }
  if (results.gapNames.length > 0) {
    console.log("\nCHARACTERIZED GAPS (doc/implementation divergences -- not suite failures):");
    for (const name of results.gapNames) console.log(`  - ${name}`);
  }
  if (results.skipNames.length > 0) {
    console.log("\nSKIPPED:");
    for (const name of results.skipNames) console.log(`  - ${name}`);
  }
  console.log("========================================");

  process.exit(results.failed === 0 ? 0 : 1);
}

main().catch((error) => {
  console.error("FATAL (suite aborted before completion)", error);
  process.exit(1);
});
