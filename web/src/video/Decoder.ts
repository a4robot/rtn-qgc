/**
 * Video decode layer — WebCodecs VideoDecoder over the bridge's binary
 * video frames (src/WebBridge/PROTOCOL.md §9).
 *
 * Framework-agnostic: no React (or any UI library) imports here. UI layers
 * (W3b VideoPlayer) adapt this via their own glue.
 *
 * Wire format (§9.2): each binary WS frame is a fixed 16-byte header followed
 * by one complete Annex-B H.264 access unit (start codes included). All
 * multi-byte header fields are little-endian per the protocol.
 *
 * Responsibilities:
 *  - parse/validate the binary frame header (pure functions, unit-tested)
 *  - wrap a WebCodecs VideoDecoder in Annex-B mode (no `description` in the
 *    config means the decoder expects in-band SPS/PPS with start codes)
 *  - wait for a keyframe before configuring/feeding; on decode error, reset
 *    and re-await a keyframe (§9.2: recovery is "wait for next keyframe")
 */

// --- binary frame header (§9.2) -----------------------------------------

/** `u16` magic at offset 0 — bytes on the wire are 0x56 0x46, ASCII "VF". */
export const VIDEO_FRAME_MAGIC = 0x4656;

/** Header `version` this parser understands. */
export const VIDEO_FRAME_VERSION = 1;

/** Fixed header size in bytes; the Annex-B access unit starts right after. */
export const VIDEO_FRAME_HEADER_BYTES = 16;

/** `flags` bit 0: this access unit is a keyframe (IDR). */
const FLAG_KEYFRAME = 0x01;

/** Decoded binary frame header plus a zero-copy view of the payload. */
export interface VideoFrameHeader {
  /** `streamIndex` from `videoConfig` this frame belongs to. */
  streamId: number;
  /** True when flags bit 0 is set — the payload is an IDR access unit. */
  keyframe: boolean;
  /** Capture/presentation time, microseconds (u64 little-endian). */
  timestampUs: bigint;
  /** The Annex-B H.264 access unit — a view into the input, not a copy. */
  payload: Uint8Array;
}

export type VideoFrameParseError = "tooShort" | "badMagic" | "badVersion";

export type VideoFrameParseResult =
  | { ok: true; header: VideoFrameHeader }
  | { ok: false; error: VideoFrameParseError };

/**
 * Parse one binary video frame. Never throws; bad frames are reported so the
 * caller can count-and-drop them (§9.2: bad magic/version is not fatal).
 */
export function parseVideoFrame(
  data: ArrayBuffer | ArrayBufferView,
): VideoFrameParseResult {
  const bytes =
    data instanceof ArrayBuffer
      ? new Uint8Array(data)
      : new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  if (bytes.byteLength < VIDEO_FRAME_HEADER_BYTES) {
    return { ok: false, error: "tooShort" };
  }

  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  // PROTOCOL.md §9.2: all multi-byte fields are little-endian.
  if (view.getUint16(0, true) !== VIDEO_FRAME_MAGIC) {
    return { ok: false, error: "badMagic" };
  }
  if (view.getUint8(2) !== VIDEO_FRAME_VERSION) {
    return { ok: false, error: "badVersion" };
  }

  return {
    ok: true,
    header: {
      streamId: view.getUint8(3),
      keyframe: (view.getUint8(4) & FLAG_KEYFRAME) !== 0,
      timestampUs: view.getBigUint64(8, true),
      payload: bytes.subarray(VIDEO_FRAME_HEADER_BYTES),
    },
  };
}

// --- codec string from in-band SPS ---------------------------------------

/**
 * Fallback when a keyframe carries no in-band SPS: Constrained Baseline,
 * level 3.0 — the most widely decodable H.264 profile.
 */
export const DEFAULT_H264_CODEC = "avc1.42E01E";

/**
 * Derive an `avc1.PPCCLL` codec string from the first SPS NAL unit in an
 * Annex-B access unit, or null when no SPS is present.
 *
 * Only the first three RBSP bytes of the SPS are read (profile_idc,
 * constraint flags, level_idc) — exactly what the codec string needs; no
 * full SPS parse. Emulation-prevention bytes cannot occur that early in a
 * valid SPS (profile_idc is never 0), so raw bytes are safe to use.
 */
export function codecStringFromAnnexB(accessUnit: Uint8Array): string | null {
  const len = accessUnit.length;
  for (let i = 0; i + 3 < len; i++) {
    // Both 3-byte (00 00 01) and 4-byte (00 00 00 01) start codes end in
    // 00 00 01, so scanning for the 3-byte form finds every NAL boundary.
    if (
      accessUnit[i] !== 0x00 ||
      accessUnit[i + 1] !== 0x00 ||
      accessUnit[i + 2] !== 0x01
    ) {
      continue;
    }
    const nalStart = i + 3;
    const nalHeader = accessUnit[nalStart];
    if (nalHeader === undefined) {
      break;
    }
    if ((nalHeader & 0x1f) === 7 && nalStart + 3 < len) {
      const profileIdc = accessUnit[nalStart + 1] ?? 0;
      const constraintFlags = accessUnit[nalStart + 2] ?? 0;
      const levelIdc = accessUnit[nalStart + 3] ?? 0;
      return `avc1.${hexByte(profileIdc)}${hexByte(constraintFlags)}${hexByte(levelIdc)}`;
    }
    i = nalStart; // skip past this start code before rescanning
  }
  return null;
}

function hexByte(value: number): string {
  return value.toString(16).padStart(2, "0").toUpperCase();
}

// --- VideoStreamDecoder ---------------------------------------------------

export type FrameHandler = (frame: VideoFrame) => void;
export type DecodeErrorHandler = (error: Error) => void;

export interface VideoStreamDecoderOptions {
  /** `streamIndex` from `videoConfig`; frames for other streams are dropped. */
  streamId: number;
  /**
   * Called with each decoded frame in presentation order. The receiver owns
   * the frame and MUST call `frame.close()` when done with it.
   */
  onFrame: FrameHandler;
  /** Called on decode/configure errors (the decoder self-recovers). */
  onError?: DecodeErrorHandler;
  /** WebCodecs hardware preference. Default "prefer-hardware". */
  hardwareAcceleration?: HardwareAcceleration;
  /** Codec string used when a keyframe has no in-band SPS. */
  fallbackCodec?: string;
}

export interface VideoStreamDecoderStats {
  /** Frames successfully decoded and delivered to `onFrame`. */
  framesDecoded: number;
  /** Binary frames dropped (bad header, wrong stream, awaiting keyframe). */
  framesDropped: number;
  /** Decode/configure errors (each one triggers a keyframe re-await). */
  errors: number;
}

/**
 * One WebCodecs VideoDecoder for one bridge video stream.
 *
 * Multi-stream: construct one instance per `streamId` — instances share
 * nothing. Feed every incoming binary WS frame to `push()`; frames for other
 * streams are cheaply dropped after the header parse, so a caller may either
 * pre-route by `parseVideoFrame(...)` or just fan the socket out to all
 * decoders.
 *
 * Lifecycle: nothing is decoded until the first keyframe arrives — the
 * decoder configures itself from that keyframe's in-band SPS (falling back
 * to `fallbackCodec`). After any decode error it resets and waits for the
 * next keyframe, matching the protocol's recovery rule.
 */
export class VideoStreamDecoder {
  readonly streamId: number;

  private readonly onFrame: FrameHandler;
  private readonly onError: DecodeErrorHandler | null;
  private readonly hardwareAcceleration: HardwareAcceleration;
  private readonly fallbackCodec: string;

  private decoder: VideoDecoder | null = null;
  private awaitingKeyframe = true;
  private closed = false;

  private framesDecoded = 0;
  private framesDropped = 0;
  private errors = 0;

  constructor(options: VideoStreamDecoderOptions) {
    this.streamId = options.streamId;
    this.onFrame = options.onFrame;
    this.onError = options.onError ?? null;
    this.hardwareAcceleration =
      options.hardwareAcceleration ?? "no-preference";
    this.fallbackCodec = options.fallbackCodec ?? DEFAULT_H264_CODEC;
  }

  /** Whether this environment has the WebCodecs VideoDecoder API. */
  static isSupported(): boolean {
    return typeof VideoDecoder !== "undefined";
  }

  get stats(): VideoStreamDecoderStats {
    return {
      framesDecoded: this.framesDecoded,
      framesDropped: this.framesDropped,
      errors: this.errors,
    };
  }

  /**
   * Feed one binary WS frame (header + Annex-B access unit). Invalid frames,
   * frames for other streams, and delta frames while awaiting a keyframe are
   * counted as dropped, never thrown.
   */
  push(data: ArrayBuffer | ArrayBufferView): void {
    if (this.closed) {
      return;
    }

    const parsed = parseVideoFrame(data);
    if (!parsed.ok) {
      this.framesDropped += 1;
      console.warn(`[VideoStreamDecoder] dropping frame: ${parsed.error}`);
      return;
    }
    const { header } = parsed;
    if (header.streamId !== this.streamId) {
      this.framesDropped += 1;
      return;
    }

    if (this.awaitingKeyframe) {
      if (!header.keyframe) {
        this.framesDropped += 1;
        return;
      }
      const codec = codecStringFromAnnexB(header.payload) ?? this.fallbackCodec;
      if (!this.configure(codec)) {
        this.framesDropped += 1;
        return;
      }
      this.awaitingKeyframe = false;
    }

    const chunk = new EncodedVideoChunk({
      type: header.keyframe ? "key" : "delta",
      timestamp: Number(header.timestampUs),
      data: header.payload,
    });
    try {
      this.decoder?.decode(chunk);
    } catch (err) {
      this.framesDropped += 1;
      this.handleError(err);
    }
  }

  /**
   * Discard decoder state and re-await a keyframe. Call when the stream
   * (re)configures — e.g. a new `videoConfig` arrives for this streamId.
   */
  reset(): void {
    if (this.closed) {
      return;
    }
    this.awaitingKeyframe = true;
    if (this.decoder && this.decoder.state !== "closed") {
      this.decoder.reset(); // back to "unconfigured"; configure() re-arms it
    }
  }

  /** Release the underlying decoder. The instance cannot be reused. */
  close(): void {
    if (this.closed) {
      return;
    }
    this.closed = true;
    if (this.decoder && this.decoder.state !== "closed") {
      this.decoder.close();
    }
    this.decoder = null;
  }

  // --- internals ---------------------------------------------------------

  private configure(codec: string): boolean {
    try {
      if (!this.decoder || this.decoder.state === "closed") {
        this.decoder = new VideoDecoder({
          output: this.handleOutput,
          error: this.handleError,
        });
      }
      // No `description`: WebCodecs then treats H.264 input as Annex-B with
      // in-band parameter sets — exactly what the bridge sends.
      this.decoder.configure({
        codec,
        optimizeForLatency: true,
        hardwareAcceleration: this.hardwareAcceleration,
      });
      return true;
    } catch (err) {
      // A hardware preference can be unsupported outright (headless/VM — no
      // GPU): retry once forcing the software decoder before giving up.
      if (this.hardwareAcceleration !== "prefer-software") {
        try {
          this.decoder?.close();
        } catch {
          /* already closed */
        }
        this.decoder = new VideoDecoder({
          output: this.handleOutput,
          error: this.handleError,
        });
        try {
          this.decoder.configure({
            codec,
            optimizeForLatency: true,
            hardwareAcceleration: "prefer-software",
          });
          return true;
        } catch (swErr) {
          this.handleError(swErr);
          return false;
        }
      }
      this.handleError(err);
      return false;
    }
  }

  private readonly handleOutput = (frame: VideoFrame): void => {
    if (this.closed) {
      frame.close();
      return;
    }
    this.framesDecoded += 1;
    this.onFrame(frame); // receiver owns the frame from here
  };

  private readonly handleError = (err: unknown): void => {
    this.errors += 1;
    this.awaitingKeyframe = true;
    // A fatal decode error closes the decoder; a caught decode() throw may
    // leave it configured. Either way, drop state and rebuild lazily on the
    // next keyframe.
    if (this.decoder) {
      if (this.decoder.state === "closed") {
        this.decoder = null;
      } else {
        try {
          this.decoder.reset();
        } catch {
          this.decoder = null;
        }
      }
    }
    const error = err instanceof Error ? err : new Error(String(err));
    console.warn(
      `[VideoStreamDecoder] stream ${this.streamId} decode error: ${error.message}`,
    );
    this.onError?.(error);
  };
}
