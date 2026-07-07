/**
 * Bridge session bootstrap — drives the PROTOCOL.md handshake over a
 * BridgeClient: on every (re)connect, send `hello` (§1.1) followed by the
 * channel subscriptions (§2.2). The bridge processes messages in order, so
 * the subscribe may ride directly behind the hello without awaiting the ack;
 * per §2.4 each (re)subscribe restarts that stream's seq at 1, which the
 * client's own seq-gap tracking already treats as a fresh stream.
 */

import type { BridgeClient } from "./BridgeClient.ts";

export interface BridgeSessionOptions {
  /** Bridge websocket URL. Default: the mock/ghost port on localhost. */
  url?: string;
  /** Vehicle whose telemetry stream to subscribe. Default 1. */
  vehicleId?: number;
}

const DEFAULT_URL = "ws://127.0.0.1:8877";

/**
 * Connect and keep the session subscribed across reconnects.
 * Returns a cleanup that unbinds the handler and disconnects the client.
 */
export function startBridgeSession(
  client: BridgeClient,
  options: BridgeSessionOptions = {},
): () => void {
  const vehicleId = options.vehicleId ?? 1;
  let handshakeSeq = 0;

  const unbindState = client.onStateChange((state) => {
    if (state !== "connected") {
      return;
    }
    handshakeSeq += 1;
    client.send({
      type: "hello",
      id: `hello-${handshakeSeq}`,
      token: "dev",
      protocolVersion: "0.1",
    });
    client.send({
      type: "subscribe",
      id: `sub-telemetry-${handshakeSeq}`,
      channel: "telemetry",
      vehicleId,
    });
  });

  client.connect(options.url ?? DEFAULT_URL);

  return () => {
    unbindState();
    client.disconnect();
  };
}
