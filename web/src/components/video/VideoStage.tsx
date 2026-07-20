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
  /** The stream ids to display in a grid. */
  streamIds: number[];
}

export function VideoStage({ client, streamIds }: VideoStageProps) {
  const [frameMetas, setFrameMetas] = useState<Record<number, FrameMeta | null>>({});

  // Dynamic grid based on number of streams
  const count = streamIds.length;
  const cols = count > 0 ? Math.ceil(Math.sqrt(count)) : 1;

  return (
    <div 
      className="video-stage"
      style={{ gridTemplateColumns: `repeat(${cols}, 1fr)` }}
    >
      {streamIds.map((id) => (
        <div key={id} className="video-stage-pane">
          <VideoPlayer 
            streamId={id} 
            client={client} 
            onFrameMeta={(meta) => setFrameMetas(prev => ({ ...prev, [id]: meta }))} 
          />
          <LatencyOverlay streamId={id} frameMeta={frameMetas[id] ?? null} />
        </div>
      ))}
    </div>
  );
}
