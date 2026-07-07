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
  CommandResponse,
  Telemetry,
  ParamValue,
} from "./types.ts";

export type ConnectionState =
  | "disconnected"
  | "connecting"
  | "connected"
  | "reconnecting";

export type MessageHandler = (message: BridgeMessage) => void;
export type CommandResponseHandler = (message: CommandResponse) => void;
export type ParamValueHandler = (message: ParamValue) => void;
export type StateHandler = (state: ConnectionState) => void;
export type SeqGapHandler = (gap: SeqGap) => void;
/** Raw binary WS frame (video, PROTOCOL.md §9.2) — undecoded, header and all. */
export type BinaryFrameHandler = (data: ArrayBuffer) => void;

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
  private readonly commandResponseHandlers = new Set<CommandResponseHandler>();
  private readonly paramValueHandlers = new Set<ParamValueHandler>();
  private readonly binaryFrameHandlers = new Set<BinaryFrameHandler>();
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

  /** Observe command acks/progress (§5, keyed by request id). Returns an unsubscribe function. */
  onCommandResponse(callback: CommandResponseHandler): () => void {
    this.commandResponseHandlers.add(callback);
    return () => {
      this.commandResponseHandlers.delete(callback);
    };
  }

  /** Observe param values (§6, keyed by request id). Returns an unsubscribe function. */
  onParamValue(callback: ParamValueHandler): () => void {
    this.paramValueHandlers.add(callback);
    return () => {
      this.paramValueHandlers.delete(callback);
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
   * Observe raw binary WS frames (video, §9.2). No parsing is done here —
   * each frame is handed to every subscriber verbatim; the video decoder
   * validates the header and filters by streamId itself. Returns an
   * unsubscribe function.
   */
  onBinaryFrame(callback: BinaryFrameHandler): () => void {
    this.binaryFrameHandlers.add(callback);
    return () => {
      this.binaryFrameHandlers.delete(callback);
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
    // §9.2: video frames arrive as binary WS messages; decode as ArrayBuffer
    // (not Blob) so handleRawMessage can route them synchronously.
    socket.binaryType = "arraybuffer";
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
    if (data instanceof ArrayBuffer) {
      // §9.2: binary video frames — no parsing here, fan out verbatim.
      for (const handler of this.binaryFrameHandlers) {
        handler(data);
      }
      return;
    }
    if (typeof data !== "string") {
      return; // e.g. Blob — shouldn't occur since binaryType is "arraybuffer"
    }

    let message: BridgeMessage;
    try {
      message = JSON.parse(data) as BridgeMessage;
    } catch {
      console.warn("[BridgeClient] dropping unparseable frame");
      return;
    }

    const type = (message as { type?: unknown })?.type;

    // §11.1: tick rides outside the channel/seq envelope — dispatch by type.
    if (type === "tick") {
      this.dispatch("tick", message);
      return;
    }

    // §5: command lifecycle is keyed by request id, not a channel stream.
    if (type === "commandAck" || type === "commandProgress") {
      for (const handler of this.commandResponseHandlers) {
        handler(message as unknown as CommandResponse);
      }
      return;
    }

    if (type === "paramValue") {
      for (const handler of this.paramValueHandlers) {
        handler(message as unknown as ParamValue);
      }
      return;
    }

    // Session acks aren't channel streams; the session layer sends the
    // requests fire-and-forget, so acks are informational only.
    if (type === "helloAck" || type === "subscribeAck" || type === "unsubscribeAck") {
      return;
    }
    if (type === "error") {
      console.warn("[BridgeClient] bridge error:", data);
      return;
    }

    const stream = message as Telemetry;
    if (typeof stream?.channel !== "string" || typeof stream?.seq !== "number") {
      console.warn("[BridgeClient] dropping malformed message", message);
      return;
    }

    this.checkSeq(stream.channel, stream.seq);
    this.dispatch(stream.channel, stream);
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
    // §2.4: seq restarts at 1 on (re)subscribe — a fresh stream, not a gap.
    if (seq === 1) {
      this.lastSeqByChannel.set(channel, seq);
      return;
    }
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

  private dispatch(key: Channel, message: BridgeMessage): void {
    const handlers = this.subscribers.get(key);
    if (handlers) {
      for (const handler of handlers) {
        handler(message);
      }
    }
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
