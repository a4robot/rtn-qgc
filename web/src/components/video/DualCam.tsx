import { useState } from "react";

import type { BridgeClient } from "../../bridge/BridgeClient.ts";
import { type FrameMeta, LatencyOverlay } from "./Overlay.tsx";
import { VideoPlayer } from "./VideoPlayer.tsx";
import "./dualcam.css";

export interface DualCamProps {
  client: BridgeClient;
  /** The two stream ids to show, left/top and right/bottom respectively. */
  streamIds: [number, number];
}

/**
 * Side-by-side dual-camera layout: two `VideoPlayer`s in a responsive grid
 * (stacks vertically once the container narrows), each with its own
 * `LatencyOverlay` corner chip. Deliberately dumb — no stream-switching
 * logic, just layout plus forwarding each pane's `onFrameMeta` frames to its
 * chip so the chip can compute its own rolling latency average.
 */
export function DualCam({ client, streamIds }: DualCamProps) {
  const [streamIdA, streamIdB] = streamIds;
  const [frameMetaA, setFrameMetaA] = useState<FrameMeta | null>(null);
  const [frameMetaB, setFrameMetaB] = useState<FrameMeta | null>(null);

  return (
    <div className="dualcam-root">
      <div className="dualcam-grid">
        <div className="dualcam-pane dualcam-pane--active">
          <VideoPlayer streamId={streamIdA} client={client} onFrameMeta={setFrameMetaA} />
          <LatencyOverlay streamId={streamIdA} frameMeta={frameMetaA} />
          <span className="dualcam-pane-label">Cam {streamIdA} · Primary</span>
        </div>
        <div className="dualcam-pane">
          <VideoPlayer streamId={streamIdB} client={client} onFrameMeta={setFrameMetaB} />
          <LatencyOverlay streamId={streamIdB} frameMeta={frameMetaB} />
          <span className="dualcam-pane-label">Cam {streamIdB}</span>
        </div>
      </div>
    </div>
  );
}
