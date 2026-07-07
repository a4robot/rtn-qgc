import { useEffect, useRef, useState } from "react";
import { VideoStreamDecoder } from "../../video/Decoder.ts";
import "./video.css";

/** How long (ms) with no decoded frame before we fall back to the placeholder. */
const SIGNAL_TIMEOUT_MS = 1500;
/** UI overlay refresh cadence (fps counter, signal/error state). Independent
 * of the draw path — draws happen via rAF only when a frame actually arrives. */
const OVERLAY_TICK_MS = 200;
/** Rolling window used for the decoded-fps counter. */
const FPS_WINDOW_MS = 1000;

export interface VideoPlayerProps {
  /** `streamIndex` this player renders; frames for other streams are ignored
   * by the underlying `VideoStreamDecoder`. */
  streamId: number;
  /**
   * Attach this player to a source of raw binary WS video frames.
   *
   * Contract: called once (on mount, and again if `streamId`/`attach`
   * change identity) with a `push` callback. The implementation must
   * arrange for every binary video-frame WS message it owns to be
   * forwarded verbatim to `push(data)` — no parsing required, the
   * decoder does its own header validation and per-stream filtering.
   * Return a detach function; it is called on unmount (or before
   * re-attaching) and must stop calling `push`.
   *
   * Why a function prop and not `client: BridgeClient`: as of this
   * writing `BridgeClient.handleRawMessage` drops every non-string
   * WS message (`typeof data !== "string"` → return, "binary frames
   * are not part of the protocol (yet)") — there is no binary-frame
   * hook to subscribe to yet. Once BridgeClient grows one, a thin
   * wrapper (`attach={(push) => client.onBinaryFrame(push)}`) can be
   * passed in without changing this component.
   *
   * Pass a stable (e.g. `useCallback`-wrapped) function — a new
   * identity every render will tear down and recreate the decoder.
   */
  attach?: (push: (data: ArrayBuffer) => void) => () => void;
  /** WebCodecs hardware preference. Forwarded to `VideoStreamDecoder`. */
  hardwareAcceleration?: HardwareAcceleration;
}

/**
 * Renders decoded WebCodecs `VideoFrame`s from a `VideoStreamDecoder` onto a
 * canvas, letterboxed in a 16:9 dark container. Overlay chrome shows the
 * stream id, a rolling decoded-fps counter, and error/no-signal placeholders.
 */
export function VideoPlayer({
  streamId,
  attach,
  hardwareAcceleration,
}: VideoPlayerProps) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const ctxRef = useRef<CanvasRenderingContext2D | null>(null);

  const latestFrameRef = useRef<VideoFrame | null>(null);
  const rafIdRef = useRef<number | null>(null);
  const fpsWindowRef = useRef<number[]>([]);
  const lastFrameAtRef = useRef<number | null>(null);

  const [supported] = useState(() => VideoStreamDecoder.isSupported());
  const [hasSignal, setHasSignal] = useState(false);
  const [fps, setFps] = useState(0);
  const [errorCount, setErrorCount] = useState(0);
  const [lastError, setLastError] = useState<string | null>(null);

  // --- decoder lifecycle: create on mount / when streamId or attach change,
  // tear down (detach, cancel rAF, close pending frame, close decoder) on
  // cleanup. ---------------------------------------------------------------
  useEffect(() => {
    if (!supported) {
      return;
    }

    const drawFrame = (frame: VideoFrame): void => {
      const canvas = canvasRef.current;
      if (!canvas) {
        frame.close();
        return;
      }
      if (canvas.width !== frame.displayWidth || canvas.height !== frame.displayHeight) {
        canvas.width = frame.displayWidth;
        canvas.height = frame.displayHeight;
      }
      let ctx = ctxRef.current;
      if (!ctx) {
        ctx = canvas.getContext("2d");
        ctxRef.current = ctx;
      }
      ctx?.drawImage(frame, 0, 0, canvas.width, canvas.height);
      frame.close();
    };

    const runDraw = (): void => {
      rafIdRef.current = null;
      const frame = latestFrameRef.current;
      if (!frame) {
        return;
      }
      latestFrameRef.current = null;
      drawFrame(frame);
    };

    const handleFrame = (frame: VideoFrame): void => {
      const now = performance.now();
      fpsWindowRef.current.push(now);
      lastFrameAtRef.current = now;

      // Keep only the latest frame; close any stale undrawn one immediately
      // so we never build an unbounded queue.
      const stale = latestFrameRef.current;
      latestFrameRef.current = frame;
      if (stale) {
        stale.close();
      }

      if (rafIdRef.current === null) {
        rafIdRef.current = requestAnimationFrame(runDraw);
      }
    };

    const handleError = (err: Error): void => {
      setErrorCount((count) => count + 1);
      setLastError(err.message);
    };

    const decoder = new VideoStreamDecoder({
      streamId,
      onFrame: handleFrame,
      onError: handleError,
      ...(hardwareAcceleration ? { hardwareAcceleration } : {}),
    });

    const detach = attach?.((data) => decoder.push(data));

    return () => {
      detach?.();
      if (rafIdRef.current !== null) {
        cancelAnimationFrame(rafIdRef.current);
        rafIdRef.current = null;
      }
      if (latestFrameRef.current) {
        latestFrameRef.current.close();
        latestFrameRef.current = null;
      }
      decoder.close();
      ctxRef.current = null;
      fpsWindowRef.current = [];
      lastFrameAtRef.current = null;
    };
  }, [streamId, attach, hardwareAcceleration, supported]);

  // --- overlay stats: decoded-fps rolling window + no-signal detection.
  // Decoupled from the draw path — this only refreshes the chrome text. ----
  useEffect(() => {
    if (!supported) {
      return;
    }
    const id = setInterval(() => {
      const now = performance.now();
      const window = fpsWindowRef.current.filter((t) => now - t <= FPS_WINDOW_MS);
      fpsWindowRef.current = window;
      setFps(window.length);

      const lastFrameAt = lastFrameAtRef.current;
      setHasSignal(lastFrameAt !== null && now - lastFrameAt < SIGNAL_TIMEOUT_MS);
    }, OVERLAY_TICK_MS);
    return () => clearInterval(id);
  }, [supported]);

  return (
    <div className="video-player">
      <div className="video-player-frame">
        <canvas ref={canvasRef} className="video-player-canvas" />

        {!supported && (
          <div className="video-player-placeholder">
            <span className="video-player-placeholder-text">
              WebCodecs not supported in this browser
            </span>
          </div>
        )}

        {supported && !hasSignal && (
          <div className="video-player-placeholder">
            <span className="video-player-placeholder-text">
              No signal — awaiting keyframe
            </span>
          </div>
        )}

        <div className="video-player-badge-row">
          <span className="video-player-badge video-player-badge-stream">
            Stream {streamId}
          </span>
          <span className="video-player-badge">{fps} fps</span>
          {errorCount > 0 && (
            <span className="video-player-badge video-player-badge-error" title={lastError ?? undefined}>
              {errorCount} decode {errorCount === 1 ? "error" : "errors"}
            </span>
          )}
        </div>
      </div>
    </div>
  );
}
