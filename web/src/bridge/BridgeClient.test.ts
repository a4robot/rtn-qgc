/**
 * BridgeClient tests — liveness watchdog (PROTOCOL.md §11.1) and seq-gap
 * resubscribe signaling (§2.4). Uses a minimal, dependency-free fake
 * WebSocket so the client can be driven deterministically without a real
 * bridge/mock server.
 */

import { beforeEach, expect, test } from "bun:test";

import { BridgeClient } from "./BridgeClient.ts";
import type { Channel } from "./types.ts";

class FakeSocket {
  static readonly CONNECTING = 0;
  static readonly OPEN = 1;
  static readonly CLOSING = 2;
  static readonly CLOSED = 3;
  static instances: FakeSocket[] = [];

  readyState = FakeSocket.CONNECTING;
  binaryType = "blob";
  url: string;
  sent: string[] = [];
  onopen: (() => void) | null = null;
  onmessage: ((ev: { data: unknown }) => void) | null = null;
  onclose: (() => void) | null = null;
  onerror: (() => void) | null = null;

  constructor(url: string) {
    this.url = url;
    FakeSocket.instances.push(this);
  }

  send(data: string): void {
    this.sent.push(data);
  }

  close(): void {
    if (this.readyState === FakeSocket.CLOSED) {
      return;
    }
    this.readyState = FakeSocket.CLOSED;
    this.onclose?.();
  }

  /** Test helper: simulate the server accepting the connection. */
  simulateOpen(): void {
    this.readyState = FakeSocket.OPEN;
    this.onopen?.();
  }

  /** Test helper: simulate an incoming text (or binary) frame. */
  simulateMessage(data: unknown): void {
    this.onmessage?.({ data } as MessageEvent);
  }
}

function installFakeWebSocket(): void {
  FakeSocket.instances = [];
  // @ts-expect-error test shim — only the members BridgeClient touches are implemented.
  globalThis.WebSocket = FakeSocket;
}

function latestSocket(): FakeSocket {
  const socket = FakeSocket.instances.at(-1);
  if (!socket) {
    throw new Error("expected a FakeSocket to have been constructed");
  }
  return socket;
}

function telemetry(seq: number): string {
  return JSON.stringify({
    type: "telemetry",
    channel: "telemetry",
    seq,
    vehicleId: 1,
    timestampMs: 1_000,
    armed: false,
    flightMode: "Hold",
    position: { lat: null, lon: null, altMSL: null, altRel: null },
    attitude: { roll: null, pitch: null, yaw: null },
    velocity: { groundSpeed: null, airSpeed: null, climbRate: null },
    battery: { percent: null, voltage: null, current: null },
    gps: { fix: "none", count: null, hdop: null },
  });
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

beforeEach(() => {
  installFakeWebSocket();
});

test("liveness: watchdog force-closes and reconnects after silence past the timeout", async () => {
  const client = new BridgeClient({ livenessTimeoutMs: 25 });
  client.connect("ws://fake");
  const socket = latestSocket();
  socket.simulateOpen();
  expect(client.connectionState).toBe("connected");

  await sleep(80);

  expect(socket.readyState).toBe(FakeSocket.CLOSED);
  expect(client.connectionState).toBe("reconnecting");

  client.disconnect();
});

test("liveness: watchdog does not fire while messages keep arriving", async () => {
  const client = new BridgeClient({ livenessTimeoutMs: 50 });
  client.connect("ws://fake");
  const socket = latestSocket();
  socket.simulateOpen();

  for (let i = 0; i < 4; i++) {
    await sleep(15); // well under the 50ms timeout, resets the watchdog each time
    socket.simulateMessage(telemetry(i + 1));
  }

  expect(client.connectionState).toBe("connected");
  expect(socket.readyState).toBe(FakeSocket.OPEN);

  client.disconnect();
});

test("liveness: no watchdog runs while disconnected", async () => {
  const client = new BridgeClient({ livenessTimeoutMs: 25 });
  client.connect("ws://fake");
  const socket = latestSocket();
  socket.simulateOpen();
  client.disconnect();

  await sleep(60); // past the timeout, but nothing should happen post-disconnect

  expect(client.connectionState).toBe("disconnected");
  expect(FakeSocket.instances).toHaveLength(1); // no phantom reconnect attempt
});

test("seq gap: fires onResubscribeNeeded once, debounced against repeat gaps", () => {
  const client = new BridgeClient({ resubscribeDebounceMs: 10_000 });
  client.connect("ws://fake");
  const socket = latestSocket();
  socket.simulateOpen();

  const seen: Channel[] = [];
  client.onResubscribeNeeded((channel) => seen.push(channel));

  socket.simulateMessage(telemetry(1)); // snapshot, establishes baseline
  socket.simulateMessage(telemetry(5)); // gap: expected 2, got 5
  expect(seen).toEqual(["telemetry"]);

  socket.simulateMessage(telemetry(9)); // still gapped, still within debounce window
  expect(seen).toEqual(["telemetry"]);

  client.disconnect();
});

test("seq gap: a fresh snapshot (seq 1) resets the stream and clears the debounce", () => {
  const client = new BridgeClient({ resubscribeDebounceMs: 10_000 });
  client.connect("ws://fake");
  const socket = latestSocket();
  socket.simulateOpen();

  const seen: Channel[] = [];
  client.onResubscribeNeeded((channel) => seen.push(channel));

  socket.simulateMessage(telemetry(1));
  socket.simulateMessage(telemetry(5)); // gap #1
  expect(seen).toEqual(["telemetry"]);

  socket.simulateMessage(telemetry(1)); // session re-subscribed; fresh snapshot lands
  socket.simulateMessage(telemetry(5)); // gap #2 — should fire again, debounce was cleared
  expect(seen).toEqual(["telemetry", "telemetry"]);

  client.disconnect();
});

test("seq gap: fires again once the debounce window elapses without a fresh snapshot", async () => {
  const client = new BridgeClient({ resubscribeDebounceMs: 20 });
  client.connect("ws://fake");
  const socket = latestSocket();
  socket.simulateOpen();

  const seen: Channel[] = [];
  client.onResubscribeNeeded((channel) => seen.push(channel));

  socket.simulateMessage(telemetry(1));
  socket.simulateMessage(telemetry(5)); // gap #1
  expect(seen).toEqual(["telemetry"]);

  await sleep(50); // past the debounce window; still no seq 1 arrived

  socket.simulateMessage(telemetry(9)); // gap continues (last seen was 5)
  expect(seen).toEqual(["telemetry", "telemetry"]);

  client.disconnect();
});

test("mission response: onMissionResponse receives missionAck and missionItems, ignores other types", () => {
  const client = new BridgeClient();
  client.connect("ws://fake");
  const socket = latestSocket();
  socket.simulateOpen();

  const seen: string[] = [];
  client.onMissionResponse((message) => seen.push(message.type));

  socket.simulateMessage(
    JSON.stringify({ type: "missionAck", id: "m-1", vehicleId: 1, status: "accepted", itemCount: 2 }),
  );
  socket.simulateMessage(
    JSON.stringify({ type: "missionItems", id: "m-2", vehicleId: 1, items: [] }),
  );
  socket.simulateMessage(
    JSON.stringify({ type: "commandAck", id: "c-1", vehicleId: 1, status: "accepted" }),
  );

  expect(seen).toEqual(["missionAck", "missionItems"]);

  client.disconnect();
});

test("mission response: unsubscribe stops delivery", () => {
  const client = new BridgeClient();
  client.connect("ws://fake");
  const socket = latestSocket();
  socket.simulateOpen();

  const seen: string[] = [];
  const unsubscribe = client.onMissionResponse((message) => seen.push(message.type));
  unsubscribe();

  socket.simulateMessage(
    JSON.stringify({ type: "missionAck", id: "m-1", vehicleId: 1, status: "rejected", reason: "seq mismatch" }),
  );

  expect(seen).toEqual([]);

  client.disconnect();
});

test("onSeqGap still observes gaps independently of the resubscribe hook", () => {
  const client = new BridgeClient({ resubscribeDebounceMs: 10_000 });
  client.connect("ws://fake");
  const socket = latestSocket();
  socket.simulateOpen();

  const gaps: Array<{ channel: Channel; expectedSeq: number; receivedSeq: number }> = [];
  client.onSeqGap((gap) => gaps.push(gap));

  socket.simulateMessage(telemetry(1));
  socket.simulateMessage(telemetry(5));

  expect(gaps).toEqual([{ channel: "telemetry", expectedSeq: 2, receivedSeq: 5 }]);

  client.disconnect();
});
