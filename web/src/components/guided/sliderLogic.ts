/**
 * Pure geometry/threshold helpers for the slide-to-confirm control
 * (Slider.tsx). Kept free of DOM/React so they can be unit tested directly
 * with `bun test` without mounting anything.
 */

/** Fraction of track travel (0..1) at/beyond which a release counts as confirmed. */
export const CONFIRM_FRACTION = 0.9;

/** Milliseconds a keyboard "hold Space" gesture must be sustained to confirm. */
export const HOLD_MS = 1000;

/** Clamp an arbitrary fraction into the valid [0, 1] drag range. */
export function clampFraction(fraction: number): number {
  if (Number.isNaN(fraction)) {
    return 0;
  }
  return Math.min(1, Math.max(0, fraction));
}

/**
 * Next thumb fraction for a pointer drag: the fraction at gesture start plus
 * the pointer's horizontal movement (`deltaPx`) as a share of the track's
 * travel distance (`travelPx`, i.e. track width minus thumb width).
 *
 * Delta-based (rather than deriving fraction from the pointer's absolute
 * position) so the thumb tracks smoothly from wherever on the thumb it was
 * grabbed, instead of jumping to meet the pointer.
 */
export function fractionFromDelta(startFraction: number, deltaPx: number, travelPx: number): number {
  if (travelPx <= 0) {
    return startFraction;
  }
  return clampFraction(startFraction + deltaPx / travelPx);
}

/** True once a drag release / keyboard hold has reached the confirm threshold. */
export function shouldConfirm(fraction: number): boolean {
  return fraction >= CONFIRM_FRACTION;
}

/** Progress fraction for a keyboard "hold Space" gesture at `elapsedMs` of `holdMs`. */
export function fractionFromHoldElapsed(elapsedMs: number, holdMs: number = HOLD_MS): number {
  if (holdMs <= 0) {
    return 1;
  }
  return clampFraction(elapsedMs / holdMs);
}
