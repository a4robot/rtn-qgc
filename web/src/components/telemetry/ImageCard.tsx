/**
 * Small collapsible card wrapping the existing `ImagePanel` (§15) for the
 * floating cockpit layout — sits above the video PiP, default-open but
 * collapsible so it stays unobtrusive. Presentation-only wrapper; the image
 * decode/store logic in ImagePanel.tsx is untouched.
 */

import { useState } from "react";

import { ImagePanel } from "./ImagePanel.tsx";
import "./image-card.css";

export interface ImageCardProps {
  vehicleId: number;
}

export function ImageCard({ vehicleId }: ImageCardProps) {
  const [open, setOpen] = useState(true);

  return (
    <div className={`image-card${open ? "" : " image-card--collapsed"}`}>
      <button
        type="button"
        className="image-card-toggle"
        onClick={() => setOpen((value) => !value)}
        aria-expanded={open}
      >
        <span aria-hidden="true">{open ? "▾" : "▸"}</span> Image
      </button>
      {open && (
        <div className="image-card-body">
          <ImagePanel vehicleId={vehicleId} />
        </div>
      )}
    </div>
  );
}
