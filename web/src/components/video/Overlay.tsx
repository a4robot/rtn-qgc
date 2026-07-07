import { useEffect, useRef, useState } from "react";

import { useConnection } from "../../store/connectionStore.ts";
import "./dualcam.css";

/** Rolling window over which the latency chip averages samples. */
const ROLLING_WINDOW_MS = 1000;
/** Chip refresh cadence — decoupled from per-frame arrival, like VideoPlayer's fps counter. */
const TICK_MS = 200;
/** Below this: green. */
const GOOD_MS = 150;
/** Below this (and at/above GOOD_MS): amber. At/above this: red. */
const WARN_MS = 400;

/** Per-frame metadata forwarded from `VideoPlayer`'s `onFrameMeta` prop. */
export interface FrameMeta {
  /** Capture/presentation time echoed from the wire header, µs since epoch. */
  timestampUs: bigint;
  /** `Date.now()` at the moment this frame was painted. */
  renderedAtMs: number;
}

export interface LatencyOverlayProps {
  /** Stream this chip reports on (label/diagnostics only — math is generic). */
  streamId: number;
  /**
   * Most recently painted frame's metadata for this stream, or null before
   * the first frame arrives. Expected to change identity on every frame the
   * owning `VideoPlayer` paints (e.g. forwarded straight from its
   * `onFrameMeta` callback via `useState`).
   */
  frameMeta: FrameMeta | null;
}

interface Sample {
  /** `performance.now()` when the sample was recorded — window bookkeeping only. */
  atMs: number;
  latencyMs: number;
}

/**
 * Corner chip showing end-to-end video latency for one stream: capture
 * (server, µs since epoch) to paint (local canvas draw), converted to a
 * common clock via `connectionStore`'s smoothed `clockOffsetMs`
 * (PROTOCOL.md §11.1) and averaged over a rolling 1 s window.
 *
 * latency ≈ renderedAt - (timestampUs/1000 - clockOffsetMs)
 *
 * Degrades to "--- ms" while `clockOffsetMs` is null (no tick received yet)
 * or before any frame has arrived.
 */
export function LatencyOverlay({ streamId, frameMeta }: LatencyOverlayProps) {
  const clockOffsetMs = useConnection((s) => s.clockOffsetMs);
  const samplesRef = useRef<Sample[]>([]);
  const [displayMs, setDisplayMs] = useState<number | null>(null);

  // Record one sample per fresh frame. Skipped (not just hidden) while the
  // clock offset is unknown, so stale offset-less samples never pollute the
  // rolling window once a tick does arrive.
  useEffect(() => {
    if (frameMeta === null || clockOffsetMs === null) {
      return;
    }
    const captureLocalMs = Number(frameMeta.timestampUs) / 1000 - clockOffsetMs;
    samplesRef.current.push({ atMs: performance.now(), latencyMs: frameMeta.renderedAtMs - captureLocalMs });
  }, [frameMeta, clockOffsetMs]);

  // Decoupled rolling-average tick.
  useEffect(() => {
    const id = setInterval(() => {
      const now = performance.now();
      const window = samplesRef.current.filter((s) => now - s.atMs <= ROLLING_WINDOW_MS);
      samplesRef.current = window;
      if (window.length === 0) {
        setDisplayMs(null);
        return;
      }
      setDisplayMs(window.reduce((sum, s) => sum + s.latencyMs, 0) / window.length);
    }, TICK_MS);
    return () => clearInterval(id);
  }, []);

  const known = clockOffsetMs !== null && displayMs !== null;
  const severity = !known ? "unknown" : displayMs! < GOOD_MS ? "good" : displayMs! < WARN_MS ? "warn" : "bad";

  return (
    <span className={`latency-chip latency-chip--${severity}`} title={`stream ${streamId} latency`}>
      {known ? `${Math.round(displayMs!)} ms` : "--- ms"}
    </span>
  );
}
