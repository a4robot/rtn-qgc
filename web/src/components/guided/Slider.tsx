/**
 * Slide-to-confirm control, QGC-style guided-action gesture.
 *
 * Drag the thumb across the track (mouse or touch, via Pointer Events +
 * `setPointerCapture` so the drag keeps tracking even once the pointer
 * leaves the thumb) and release at or past `CONFIRM_FRACTION` (90%) to fire
 * `onConfirm` exactly once; releasing earlier animates the thumb back to
 * the start with no action. All fraction/threshold math lives in
 * `sliderLogic.ts` so it's unit-testable without mounting this component.
 *
 * Keyboard fallback (WCAG 2.1.1 — must be operable without a pointer):
 * focus the control (Tab) and hold Space for `HOLD_MS` (1s). The thumb
 * fills to show hold progress, exactly mirroring the drag gesture; releasing
 * Space before the hold completes cancels with no action, matching the
 * drag's "commit or abort" semantics. (A double-press-Enter pattern was
 * considered but rejected — a timed hold maps more directly to "slide the
 * full distance" than a discrete double-tap does.)
 */

import { useCallback, useEffect, useRef, useState } from "react";
import type { KeyboardEvent as ReactKeyboardEvent, PointerEvent as ReactPointerEvent } from "react";

import { fractionFromDelta, fractionFromHoldElapsed, HOLD_MS, shouldConfirm } from "./sliderLogic.ts";
import "./slider.css";

/** Must match the thumb width baked into slider.css (`.slider-thumb`). */
const THUMB_SIZE_PX = 28;
/** Duration of the "snap back to start" animation after a release/cancel. */
const SNAP_BACK_MS = 200;

export interface SliderProps {
  label: string;
  onConfirm: () => void;
  disabled?: boolean;
  danger?: boolean;
  /** Extra class(es) appended to the root, e.g. for caller-driven status tinting. */
  className?: string;
}

export function Slider({ label, onConfirm, disabled, danger, className }: SliderProps) {
  const trackRef = useRef<HTMLDivElement>(null);
  const travelPxRef = useRef(0);
  const fractionRef = useRef(0);
  const dragRef = useRef<{ pointerId: number; startClientX: number; startFraction: number } | null>(null);
  const holdStartRef = useRef<number | null>(null);
  const holdRafRef = useRef<number | null>(null);
  const snapTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const [fraction, setFractionState] = useState(0);
  const [dragging, setDragging] = useState(false);
  const [holding, setHolding] = useState(false);
  const [snapping, setSnapping] = useState(false);

  const setFraction = useCallback((next: number) => {
    fractionRef.current = next;
    setFractionState(next);
  }, []);

  const measureTravelPx = useCallback(() => {
    const track = trackRef.current;
    if (!track) {
      return 0;
    }
    return Math.max(track.getBoundingClientRect().width - THUMB_SIZE_PX, 0);
  }, []);

  const reset = useCallback(() => {
    setDragging(false);
    setHolding(false);
    setFraction(0);
    setSnapping(true);
    if (snapTimerRef.current !== null) {
      clearTimeout(snapTimerRef.current);
    }
    snapTimerRef.current = setTimeout(() => {
      snapTimerRef.current = null;
      setSnapping(false);
    }, SNAP_BACK_MS);
  }, [setFraction]);

  const cancelHoldLoop = useCallback(() => {
    if (holdRafRef.current !== null) {
      cancelAnimationFrame(holdRafRef.current);
      holdRafRef.current = null;
    }
    holdStartRef.current = null;
  }, []);

  // Cleanup any pending timers/RAF on unmount.
  useEffect(() => {
    return () => {
      cancelHoldLoop();
      if (snapTimerRef.current !== null) {
        clearTimeout(snapTimerRef.current);
      }
    };
  }, [cancelHoldLoop]);

  const handlePointerDown = useCallback(
    (event: ReactPointerEvent<HTMLDivElement>) => {
      if (disabled) {
        return;
      }
      travelPxRef.current = measureTravelPx();
      dragRef.current = {
        pointerId: event.pointerId,
        startClientX: event.clientX,
        startFraction: fractionRef.current,
      };
      event.currentTarget.setPointerCapture(event.pointerId);
      setSnapping(false);
      setDragging(true);
    },
    [disabled, measureTravelPx],
  );

  const handlePointerMove = useCallback((event: ReactPointerEvent<HTMLDivElement>) => {
    const drag = dragRef.current;
    if (!drag || drag.pointerId !== event.pointerId) {
      return;
    }
    const next = fractionFromDelta(drag.startFraction, event.clientX - drag.startClientX, travelPxRef.current);
    setFraction(next);
  }, [setFraction]);

  const endDrag = useCallback(
    (event: ReactPointerEvent<HTMLDivElement>) => {
      const drag = dragRef.current;
      if (!drag || drag.pointerId !== event.pointerId) {
        return;
      }
      dragRef.current = null;
      const confirmed = shouldConfirm(fractionRef.current);
      reset();
      if (confirmed) {
        onConfirm();
      }
    },
    [onConfirm, reset],
  );

  const tickHold = useCallback(() => {
    if (holdStartRef.current === null) {
      return;
    }
    const elapsedMs = Date.now() - holdStartRef.current;
    setFraction(fractionFromHoldElapsed(elapsedMs, HOLD_MS));
    if (elapsedMs >= HOLD_MS) {
      cancelHoldLoop();
      reset();
      onConfirm();
      return;
    }
    holdRafRef.current = requestAnimationFrame(tickHold);
  }, [cancelHoldLoop, onConfirm, reset, setFraction]);

  const handleKeyDown = useCallback(
    (event: ReactKeyboardEvent<HTMLDivElement>) => {
      if (disabled || (event.key !== " " && event.key !== "Spacebar")) {
        return;
      }
      event.preventDefault();
      if (event.repeat || holdStartRef.current !== null) {
        return;
      }
      travelPxRef.current = measureTravelPx();
      holdStartRef.current = Date.now();
      setHolding(true);
      setSnapping(false);
      holdRafRef.current = requestAnimationFrame(tickHold);
    },
    [disabled, measureTravelPx, tickHold],
  );

  const handleKeyUp = useCallback(
    (event: ReactKeyboardEvent<HTMLDivElement>) => {
      if (event.key !== " " && event.key !== "Spacebar") {
        return;
      }
      event.preventDefault();
      if (holdStartRef.current === null) {
        // Already completed (confirmed) or never started — nothing to cancel.
        return;
      }
      cancelHoldLoop();
      reset();
    },
    [cancelHoldLoop, reset],
  );

  const thumbLeft = `calc(${fraction * 100}% - ${fraction * THUMB_SIZE_PX}px)`;

  return (
    <div
      className={[
        "slider-root",
        danger ? "slider-root--danger" : "",
        disabled ? "slider-root--disabled" : "",
        dragging ? "slider-root--dragging" : "",
        holding ? "slider-root--holding" : "",
        className ?? "",
      ]
        .filter(Boolean)
        .join(" ")}
      role="slider"
      aria-label={`Slide to ${label.toLowerCase()}`}
      aria-valuemin={0}
      aria-valuemax={100}
      aria-valuenow={Math.round(fraction * 100)}
      aria-disabled={disabled || undefined}
      tabIndex={disabled ? -1 : 0}
      onKeyDown={handleKeyDown}
      onKeyUp={handleKeyUp}
    >
      <div className="slider-track" ref={trackRef}>
        <div className="slider-fill" style={{ width: `${fraction * 100}%` }} aria-hidden="true" />
        <span className="slider-label" aria-hidden="true">
          {holding ? "Hold to confirm…" : `Slide to ${label}`}
        </span>
        <div
          className={`slider-thumb${snapping ? " slider-thumb--snapping" : ""}`}
          style={{ left: thumbLeft }}
          onPointerDown={handlePointerDown}
          onPointerMove={handlePointerMove}
          onPointerUp={endDrag}
          onPointerCancel={endDrag}
          aria-hidden="true"
        >
          »
        </div>
      </div>
    </div>
  );
}
