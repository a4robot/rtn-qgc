/**
 * BridgeClient — typed WebSocket client for the GCS bridge.
 *
 * Framework-agnostic: no React (or any UI library) imports here. UI layers
 * adapt this via their own glue (e.g. a useBridge hook).
 *
 * Responsibilities:
 *  - connect/reconnect with exponential backoff
 *  - fan incoming channel messages out to subscribers
 *  - liveness watchdog (PROTOCOL.md §11.1): any message (of any kind) resets
 *    a 5s timer while connected; silence past that forces the socket closed
 *    and lets the normal reconnect path take over
 *  - detect per-channel seq gaps (PROTOCOL.md §2.4) and notify via
 *    `onResubscribeNeeded` — the client doesn't know subscribe payloads
 *    (vehicleId/streamId live in the session layer), so it only signals
 *    *which* channel needs a fresh subscribe; the caller re-sends it
 */

import type {
  BridgeMessage,
  Channel,
  ClientMessage,
  CommandAck,
  CommandResponse,
  MissionAck,
  MissionResponse,
  Notification,
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
export type MissionResponseHandler = (message: MissionResponse) => void;
export type NotificationHandler = (message: Notification) => void;
export type StateHandler = (state: ConnectionState) => void;
export type SeqGapHandler = (gap: SeqGap) => void;
/**
 * Fired when a channel needs a fresh subscribe (seq gap, PROTOCOL.md §2.4).
 * The client only knows *which* channel gapped — it has no idea what
 * vehicleId/streamId payload the subscribe needs, so the session layer
 * (which owns that) is expected to re-send the appropriate subscribe(s).
 */
export type ResubscribeHandler = (channel: Channel) => void;
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
  /**
   * Liveness watchdog timeout in ms (PROTOCOL.md §11.1: "no message of any
   * kind for 5 s" => dead connection). Default 5_000.
   */
  livenessTimeoutMs?: number;
  /**
   * How long a channel's resubscribe-needed signal stays debounced: once
   * fired, further gaps on the same channel are suppressed until either a
   * fresh snapshot (seq 1) arrives or this much time passes. Default 2_000.
   */
  resubscribeDebounceMs?: number;
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
  private readonly livenessTimeoutMs: number;
  private readonly resubscribeDebounceMs: number;

  /** §11.1 liveness watchdog: reset by any incoming message while connected. */
  private livenessTimer: ReturnType<typeof setTimeout> | null = null;
  /** §2.4 per-channel debounce: one resubscribe signal in flight at a time. */
  private readonly resubscribePending = new Map<Channel, ReturnType<typeof setTimeout>>();

  private readonly subscribers = new Map<Channel, Set<MessageHandler>>();
  private readonly stateHandlers = new Set<StateHandler>();
  private readonly seqGapHandlers = new Set<SeqGapHandler>();
  private readonly resubscribeHandlers = new Set<ResubscribeHandler>();
  private readonly commandResponseHandlers = new Set<CommandResponseHandler>();
  private readonly paramValueHandlers = new Set<ParamValueHandler>();
  private readonly missionResponseHandlers = new Set<MissionResponseHandler>();
  private readonly notificationHandlers = new Set<NotificationHandler>();
  private readonly binaryFrameHandlers = new Set<BinaryFrameHandler>();
  private readonly lastSeqByChannel = new Map<Channel, number>();

  constructor(options: BridgeClientOptions = {}) {
    this.baseDelayMs = options.reconnectBaseDelayMs ?? 500;
    this.maxDelayMs = options.reconnectMaxDelayMs ?? 10_000;
    this.backoffFactor = options.reconnectBackoffFactor ?? 2;
    this.livenessTimeoutMs = options.livenessTimeoutMs ?? 5_000;
    this.resubscribeDebounceMs = options.resubscribeDebounceMs ?? 2_000;
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
    this.clearAllResubscribePending();
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

  /** Observe mission upload/download/clear responses (§7.2, keyed by request id). Returns an unsubscribe function. */
  onMissionResponse(callback: MissionResponseHandler): () => void {
    this.missionResponseHandlers.add(callback);
    return () => {
      this.missionResponseHandlers.delete(callback);
    };
  }

  /**
   * Observe notification pushes (PROTOCOL.md §14, keyed by nothing — pure
   * server-push, no subscription, no request id; mirrors {@link
   * onCommandResponse}). Returns an unsubscribe function.
   */
  onNotification(callback: NotificationHandler): () => void {
    this.notificationHandlers.add(callback);
    return () => {
      this.notificationHandlers.delete(callback);
    };
  }

  /** Observe detected seq gaps (observability only). Returns an unsubscribe function. */
  onSeqGap(callback: SeqGapHandler): () => void {
    this.seqGapHandlers.add(callback);
    return () => {
      this.seqGapHandlers.delete(callback);
    };
  }

  /**
   * Observe channels that need a fresh subscribe after a seq gap (§2.4).
   * The client has no knowledge of subscribe payloads (vehicleId/streamId
   * live in the session layer) — this only says *which* channel gapped.
   * Debounced per channel: fires once per gap "episode" (see
   * {@link BridgeClientOptions.resubscribeDebounceMs}). Returns an
   * unsubscribe function.
   */
  onResubscribeNeeded(callback: ResubscribeHandler): () => void {
    this.resubscribeHandlers.add(callback);
    return () => {
      this.resubscribeHandlers.delete(callback);
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
      this.clearAllResubscribePending();
      this.setState("connected");
      this.armLivenessTimer();
    };

    socket.onmessage = (event: MessageEvent) => {
      if (socket !== this.socket) {
        return;
      }
      // §11.1: ANY message (parseable or not, text or binary) counts as
      // liveness — reset the watchdog before we even look at the payload.
      this.armLivenessTimer();
      this.handleRawMessage(event.data);
    };

    socket.onclose = () => {
      if (socket !== this.socket) {
        return;
      }
      this.socket = null;
      this.clearLivenessTimer();
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
    this.clearLivenessTimer();
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

  /**
   * §11.1 liveness: (re)arm the "dead connection" timer. Called on connect
   * and on every incoming message; a fresh timer replaces any pending one.
   */
  private armLivenessTimer(): void {
    if (this.livenessTimer !== null) {
      clearTimeout(this.livenessTimer);
    }
    this.livenessTimer = setTimeout(() => {
      this.livenessTimer = null;
      this.handleLivenessTimeout();
    }, this.livenessTimeoutMs);
  }

  private clearLivenessTimer(): void {
    if (this.livenessTimer !== null) {
      clearTimeout(this.livenessTimer);
      this.livenessTimer = null;
    }
  }

  /**
   * §11.1: no message of any kind for `livenessTimeoutMs` — the connection
   * is considered dead. Force-close the socket (without marking this an
   * intentional close) so `onclose` runs the normal reconnect path, which
   * re-runs the session's hello+subscribe handshake.
   */
  private handleLivenessTimeout(): void {
    console.warn(
      `[BridgeClient] no messages for ${this.livenessTimeoutMs}ms; treating connection as dead`,
    );
    this.socket?.close();
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

    // §14: notification — server-push, no subscription/channel/seq envelope, dispatched by type (like tick).
    if (type === "notification") {
      for (const handler of this.notificationHandlers) {
        handler(message as unknown as Notification);
      }
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

    // §7.2: mission upload/download/clear responses are keyed by request id, not a channel stream.
    if (type === "missionAck" || type === "missionItems") {
      for (const handler of this.missionResponseHandlers) {
        handler(message as unknown as MissionResponse);
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
      // PROTOCOL.md §10: a request-answering error (one that echoes the request `id`, e.g.
      // CommandChannel/MissionChannel's UNKNOWN_VEHICLE) is the *only* response a `command` or
      // mission* request will ever get in that case — no commandAck/missionAck follows. The
      // request-tracking UI (useCommandRequests, WaypointList) only understands the ack shapes,
      // so translate a keyed error into a synthetic rejected ack and fan it out to both response
      // handler sets (the id only matches whichever store is actually tracking it; the other
      // dispatch is a harmless no-op, same as any unrecognized id already is for those reducers).
      // Unsolicited errors (no `id`, e.g. AUTH_REQUIRED/BAD_MESSAGE for a malformed frame) are
      // intentionally left as the console.warn above only — there is no in-flight request to
      // resolve.
      const err = message as { id?: unknown; code?: unknown; message?: unknown; vehicleId?: unknown };
      if (typeof err.id === "string" && err.id.length > 0) {
        const vehicleId = typeof err.vehicleId === "number" ? err.vehicleId : -1;
        const reason = typeof err.message === "string" ? err.message : typeof err.code === "string" ? err.code : "error";
        const commandRejection: CommandAck = { type: "commandAck", id: err.id, vehicleId, status: "rejected", reason };
        for (const handler of this.commandResponseHandlers) {
          handler(commandRejection);
        }
        const missionRejection: MissionAck = { type: "missionAck", id: err.id, vehicleId, status: "rejected", reason };
        for (const handler of this.missionResponseHandlers) {
          handler(missionRejection);
        }
      }
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
   * Seq-gap detection (PROTOCOL.md §2.4).
   *
   * State-not-events protocol: a gap never requires replay — a fresh
   * subscribe/snapshot supersedes anything missed. On a gap we notify
   * observers (`onSeqGap`) and signal `onResubscribeNeeded` so the layer
   * that owns subscribe payloads (session) can re-subscribe the channel.
   */
  private checkSeq(channel: Channel, seq: number): void {
    const last = this.lastSeqByChannel.get(channel);
    // §2.4: seq restarts at 1 on (re)subscribe — a fresh stream, not a gap.
    if (seq === 1) {
      this.lastSeqByChannel.set(channel, seq);
      // The resubscribe we asked for (or an independent one) landed.
      this.clearResubscribePending(channel);
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
      this.requestResubscribe(channel);
    }
    this.lastSeqByChannel.set(channel, seq);
  }

  /**
   * Fire `onResubscribeNeeded` for `channel`, debounced: once fired, further
   * calls are suppressed until either a fresh snapshot (seq 1) arrives via
   * {@link checkSeq} or `resubscribeDebounceMs` passes, whichever is first.
   */
  private requestResubscribe(channel: Channel): void {
    if (this.resubscribePending.has(channel)) {
      return; // already in flight — wait for seq 1 or the debounce window
    }
    const timer = setTimeout(() => {
      this.resubscribePending.delete(channel);
    }, this.resubscribeDebounceMs);
    this.resubscribePending.set(channel, timer);
    for (const handler of this.resubscribeHandlers) {
      handler(channel);
    }
  }

  private clearResubscribePending(channel: Channel): void {
    const timer = this.resubscribePending.get(channel);
    if (timer !== undefined) {
      clearTimeout(timer);
      this.resubscribePending.delete(channel);
    }
  }

  private clearAllResubscribePending(): void {
    for (const timer of this.resubscribePending.values()) {
      clearTimeout(timer);
    }
    this.resubscribePending.clear();
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
