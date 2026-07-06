/**
 * BridgeClient — typed WebSocket client for the GCS bridge.
 *
 * Framework-agnostic: no React (or any UI library) imports here. UI layers
 * adapt this via their own glue (e.g. a useBridge hook).
 *
 * Responsibilities:
 *  - connect/reconnect with exponential backoff
 *  - fan incoming channel messages out to subscribers
 *  - detect per-channel seq gaps (stub: logs + notifies; recovery is
 *    "ask bridge for fresh snapshot", to be implemented with the protocol)
 */

import type {
  BridgeMessage,
  Channel,
  ClientMessage,
} from "./types.ts";

export type ConnectionState =
  | "disconnected"
  | "connecting"
  | "connected"
  | "reconnecting";

export type MessageHandler = (message: BridgeMessage) => void;
export type StateHandler = (state: ConnectionState) => void;
export type SeqGapHandler = (gap: SeqGap) => void;

export interface SeqGap {
  channel: Channel;
  expectedSeq: number;
  receivedSeq: number;
}

export interface BridgeClientOptions {
  /** Initial reconnect delay in ms. Default 500. */
  reconnectBaseDelayMs?: number;
  /** Cap for the backoff delay in ms. Default 10_000. */
  reconnectMaxDelayMs?: number;
  /** Multiplier applied to the delay after each failed attempt. Default 2. */
  reconnectBackoffFactor?: number;
}

export class BridgeClient {
  private url: string | null = null;
  private socket: WebSocket | null = null;
  private state: ConnectionState = "disconnected";
  private intentionalClose = false;

  private reconnectAttempts = 0;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private readonly baseDelayMs: number;
  private readonly maxDelayMs: number;
  private readonly backoffFactor: number;

  private readonly subscribers = new Map<Channel, Set<MessageHandler>>();
  private readonly stateHandlers = new Set<StateHandler>();
  private readonly seqGapHandlers = new Set<SeqGapHandler>();
  private readonly lastSeqByChannel = new Map<Channel, number>();

  constructor(options: BridgeClientOptions = {}) {
    this.baseDelayMs = options.reconnectBaseDelayMs ?? 500;
    this.maxDelayMs = options.reconnectMaxDelayMs ?? 10_000;
    this.backoffFactor = options.reconnectBackoffFactor ?? 2;
  }

  get connectionState(): ConnectionState {
    return this.state;
  }

  /** Open (or re-target) the bridge connection. */
  connect(url: string): void {
    this.url = url;
    this.intentionalClose = false;
    this.clearReconnectTimer();
    this.closeSocket();
    this.openSocket();
  }

  /** Close the connection and stop reconnecting. */
  disconnect(): void {
    this.intentionalClose = true;
    this.clearReconnectTimer();
    this.closeSocket();
    this.setState("disconnected");
  }

  /**
   * Subscribe to messages on a channel. Returns an unsubscribe function.
   */
  subscribe(channel: Channel, callback: MessageHandler): () => void {
    let handlers = this.subscribers.get(channel);
    if (!handlers) {
      handlers = new Set();
      this.subscribers.set(channel, handlers);
    }
    handlers.add(callback);
    return () => {
      handlers.delete(callback);
    };
  }

  /** Observe connection state changes. Returns an unsubscribe function. */
  onStateChange(callback: StateHandler): () => void {
    this.stateHandlers.add(callback);
    return () => {
      this.stateHandlers.delete(callback);
    };
  }

  /** Observe detected seq gaps. Returns an unsubscribe function. */
  onSeqGap(callback: SeqGapHandler): () => void {
    this.seqGapHandlers.add(callback);
    return () => {
      this.seqGapHandlers.delete(callback);
    };
  }

  /**
   * Send a message to the bridge.
   * Returns false if the socket is not open (message is dropped, not queued
   * — commands must reflect *current* user intent, stale ones are dangerous).
   */
  send(msg: ClientMessage): boolean {
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) {
      return false;
    }
    this.socket.send(JSON.stringify(msg));
    return true;
  }

  // --- internals ---------------------------------------------------------

  private openSocket(): void {
    if (!this.url) {
      return;
    }
    this.setState(this.reconnectAttempts > 0 ? "reconnecting" : "connecting");

    const socket = new WebSocket(this.url);
    this.socket = socket;

    socket.onopen = () => {
      if (socket !== this.socket) {
        return; // superseded by a newer connect()
      }
      this.reconnectAttempts = 0;
      // Fresh session: seq numbering restarts on the bridge side.
      this.lastSeqByChannel.clear();
      this.setState("connected");
    };

    socket.onmessage = (event: MessageEvent) => {
      if (socket !== this.socket) {
        return;
      }
      this.handleRawMessage(event.data);
    };

    socket.onclose = () => {
      if (socket !== this.socket) {
        return;
      }
      this.socket = null;
      if (this.intentionalClose) {
        this.setState("disconnected");
        return;
      }
      this.scheduleReconnect();
    };

    socket.onerror = () => {
      // onclose fires after onerror; reconnect is handled there.
    };
  }

  private closeSocket(): void {
    if (this.socket) {
      const socket = this.socket;
      this.socket = null;
      socket.onopen = null;
      socket.onmessage = null;
      socket.onclose = null;
      socket.onerror = null;
      socket.close();
    }
  }

  private scheduleReconnect(): void {
    this.setState("reconnecting");
    const delay = Math.min(
      this.baseDelayMs * this.backoffFactor ** this.reconnectAttempts,
      this.maxDelayMs,
    );
    this.reconnectAttempts += 1;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.openSocket();
    }, delay);
  }

  private clearReconnectTimer(): void {
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    this.reconnectAttempts = 0;
  }

  private handleRawMessage(data: unknown): void {
    if (typeof data !== "string") {
      return; // binary frames are not part of the protocol (yet)
    }

    let message: BridgeMessage;
    try {
      message = JSON.parse(data) as BridgeMessage;
    } catch {
      console.warn("[BridgeClient] dropping unparseable frame");
      return;
    }
    if (typeof message?.channel !== "string" || typeof message?.seq !== "number") {
      console.warn("[BridgeClient] dropping malformed message", message);
      return;
    }

    this.checkSeq(message.channel, message.seq);

    const handlers = this.subscribers.get(message.channel);
    if (handlers) {
      for (const handler of handlers) {
        handler(message);
      }
    }
  }

  /**
   * Seq-gap detection (stub).
   *
   * State-not-events protocol: a gap never requires replay — the next
   * snapshot supersedes anything missed. For now we just record and notify;
   * later this will trigger an explicit snapshot request to the bridge.
   */
  private checkSeq(channel: Channel, seq: number): void {
    const last = this.lastSeqByChannel.get(channel);
    if (last !== undefined && seq !== last + 1) {
      const gap: SeqGap = {
        channel,
        expectedSeq: last + 1,
        receivedSeq: seq,
      };
      console.warn(
        `[BridgeClient] seq gap on "${channel}": expected ${gap.expectedSeq}, got ${gap.receivedSeq}`,
      );
      for (const handler of this.seqGapHandlers) {
        handler(gap);
      }
      // TODO(W0b): request a fresh snapshot for this channel from the bridge.
    }
    this.lastSeqByChannel.set(channel, seq);
  }

  private setState(state: ConnectionState): void {
    if (this.state === state) {
      return;
    }
    this.state = state;
    for (const handler of this.stateHandlers) {
      handler(state);
    }
  }
}
