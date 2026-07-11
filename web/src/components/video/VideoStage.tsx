/**
 * PiP-aware video stage — QGC's `FlyViewVideo`/`FlyViewVideo2` pair
 * presented as a single slot with a stream-switcher chip, instead of
 * `DualCam`'s side-by-side layout. Reuses `VideoPlayer`/`LatencyOverlay`
 * (Overlay.tsx) verbatim — the WebCodecs decode pipeline is untouched, this
 * component only decides which decoded stream is visually shown.
 *
 * Both streams stay mounted/decoding at all times (like `DualCam` already
 * does) so flipping the visible one via the 1|2 chip is instant; the hidden
 * pane keeps painting to its own offscreen canvas via `visibility: hidden`
 * rather than `display: none`, so no canvas gets torn down and recreated.
 *
 * This component owns only which stream is active — not fullscreen/PiP
 * placement, which is the caller's `.stage-slot` CSS (see App.tsx).
 */

import { useState } from "react";

import type { BridgeClient } from "../../bridge/BridgeClient.ts";
import { type FrameMeta, LatencyOverlay } from "./Overlay.tsx";
import { VideoPlayer } from "./VideoPlayer.tsx";
import "./video-stage.css";

export interface VideoStageProps {
  client: BridgeClient;
  /** The two stream ids selectable via the switcher chip. */
  streamIds: [number, number];
}

export function VideoStage({ client, streamIds }: VideoStageProps) {
  const [activeIndex, setActiveIndex] = useState<0 | 1>(0);
  const [frameMetaA, setFrameMetaA] = useState<FrameMeta | null>(null);
  const [frameMetaB, setFrameMetaB] = useState<FrameMeta | null>(null);

  const frameMeta = activeIndex === 0 ? frameMetaA : frameMetaB;

  return (
    <div className="video-stage">
      <div className={`video-stage-pane${activeIndex === 0 ? " video-stage-pane--active" : ""}`}>
        <VideoPlayer streamId={streamIds[0]} client={client} onFrameMeta={setFrameMetaA} />
      </div>
      <div className={`video-stage-pane${activeIndex === 1 ? " video-stage-pane--active" : ""}`}>
        <VideoPlayer streamId={streamIds[1]} client={client} onFrameMeta={setFrameMetaB} />
      </div>

      <LatencyOverlay streamId={streamIds[activeIndex]} frameMeta={frameMeta} />

      <div className="video-stage-switcher" role="group" aria-label="Video stream select">
        {streamIds.map((id, index) => (
          <button
            key={id}
            type="button"
            className={`video-stage-switch-btn${activeIndex === index ? " video-stage-switch-btn--active" : ""}`}
            aria-pressed={activeIndex === index}
            onClick={(event) => {
              // Don't let the switcher click bubble up into the PiP's
              // click-to-swap handler (App.tsx) — picking a stream should
              // never also flip map<->video fullscreen.
              event.stopPropagation();
              setActiveIndex(index as 0 | 1);
            }}
          >
            {id}
          </button>
        ))}
      </div>
    </div>
  );
}
