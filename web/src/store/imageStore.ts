/**
 * Image store — latest MAVLink image-transmission-protocol frame per vehicle,
 * keyed by vehicleId (PROTOCOL.md §15, Q8d).
 *
 * State-not-events, same as vehicleStore/missionStore: every `image` message
 * is the vehicle's current "latest image" state, so applying one simply
 * replaces that vehicle's entry. `count` is local bookkeeping (not part of
 * the wire payload) so the UI/tests can distinguish "no image yet" from
 * "same image resent" from "new image arrived" without diffing `data`.
 */

import { create } from "zustand";

import type { ImageFormat, ImageState } from "../bridge/types.ts";

/** Renderable per-vehicle image state: the payload minus the channel envelope. */
export interface ImageRecord {
  imageIndex: number;
  format: ImageFormat;
  width: number;
  height: number;
  /** Base64 of the raw reassembled bytes — decode per `format` (PROTOCOL.md §15.3). */
  data: string;
  /** Local receipt time of the last `image` message, ms since epoch. */
  lastUpdateAtMs: number;
  /** How many `image` messages this vehicle has received this session (bookkeeping, not wire data). */
  count: number;
}

export interface ImageStoreState {
  /** Latest image per vehicle, keyed by MAVLink system id. */
  images: Record<number, ImageRecord>;

  applyImage: (image: ImageState, receivedAtMs?: number) => void;
  /** Drop all image state (e.g. on disconnect if a hard reset is wanted). */
  clear: () => void;
}

export const useImageStore = create<ImageStoreState>()((set) => ({
  images: {},

  applyImage: (image, receivedAtMs = Date.now()) =>
    set((prev) => {
      const previousCount = prev.images[image.vehicleId]?.count ?? 0;
      return {
        images: {
          ...prev.images,
          [image.vehicleId]: {
            imageIndex: image.imageIndex,
            format: image.format,
            width: image.width,
            height: image.height,
            data: image.data,
            lastUpdateAtMs: receivedAtMs,
            count: previousCount + 1,
          },
        },
      };
    }),

  clear: () => set({ images: {} }),
}));

/** Latest image for one vehicle, or undefined if never seen. */
export function useImage(vehicleId: number): ImageRecord | undefined {
  return useImageStore((state) => state.images[vehicleId]);
}

/** All known latest images keyed by vehicleId. */
export function useImages(): Record<number, ImageRecord> {
  return useImageStore((state) => state.images);
}
