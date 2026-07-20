/**
 * MAVLink image-transmission-protocol panel (PROTOCOL.md §15, Q8d) — shows
 * the latest image received for a vehicle (mainly optical flow camera
 * frames). Read-only, small, tucked into the telemetry column next to
 * Attitude/Status.
 *
 * Decode split by `format` (§15.3's client decode guidance):
 *  - jpeg/png/bmp: browser-native via a `data:` URI <img>.
 *  - raw8u: one grayscale byte per pixel, row-major — decoded onto a
 *    <canvas> via ImageData (no browser-native container for this format).
 *  - pgm/raw32u/unknown: no decoder here — shown as a labeled placeholder
 *    with the byte count, still proving the channel is delivering data.
 */

import { useEffect, useMemo, useRef } from "react";
import { useTranslation } from "react-i18next";

import type { ImageFormat } from "../../bridge/types.ts";
import { useImage } from "../../store/index.ts";
import "./image-panel.css";

export interface ImagePanelProps {
  vehicleId: number;
}

const NATIVE_CONTAINER_FORMATS: ReadonlySet<ImageFormat> = new Set(["jpeg", "png", "bmp"]);

/** base64 -> Uint8Array, browser-safe (no Buffer dependency). */
function base64ToBytes(b64: string): Uint8Array {
  const binary = atob(b64);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) {
    bytes[i] = binary.charCodeAt(i);
  }
  return bytes;
}

/** Renders a raw8u (grayscale, row-major, width*height bytes) payload onto a canvas. */
function Raw8uCanvas({ data, width, height }: { data: string; width: number; height: number }) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || width <= 0 || height <= 0) {
      return;
    }
    const ctx = canvas.getContext("2d");
    if (!ctx) {
      return;
    }
    const gray = base64ToBytes(data);
    const rgba = ctx.createImageData(width, height);
    const n = Math.min(gray.length, width * height);
    for (let i = 0; i < n; i++) {
      const v = gray[i]!;
      rgba.data[i * 4 + 0] = v;
      rgba.data[i * 4 + 1] = v;
      rgba.data[i * 4 + 2] = v;
      rgba.data[i * 4 + 3] = 255;
    }
    ctx.putImageData(rgba, 0, 0);
  }, [data, width, height]);

  return <canvas ref={canvasRef} width={width} height={height} className="image-panel-canvas" />;
}

export function ImagePanel({ vehicleId }: ImagePanelProps) {
  const image = useImage(vehicleId);
  const { t } = useTranslation();

  const dataUri = useMemo(() => {
    if (!image || !NATIVE_CONTAINER_FORMATS.has(image.format)) {
      return null;
    }
    return `data:image/${image.format};base64,${image.data}`;
  }, [image]);

  if (!image) {
    return (
      <div className="image-panel image-panel--empty" aria-label={t("Latest image")}>
        <span className="image-panel-placeholder">{t("No image yet")}</span>
      </div>
    );
  }

  return (
    <div className="image-panel" aria-label={t("Latest image")}>
      <div className="image-panel-header">
        <span className="image-panel-title">{t("Image #{{index}}", { index: image.imageIndex })}</span>
        <span className="image-panel-meta">
          {image.format} · {image.width}×{image.height} · {t("{{count}} received", { count: image.count })}
        </span>
      </div>
      <div className="image-panel-body">
        {dataUri ? (
          <img src={dataUri} alt={t("Latest {{format}} frame", { format: image.format })} className="image-panel-img" />
        ) : image.format === "raw8u" ? (
          <Raw8uCanvas data={image.data} width={image.width} height={image.height} />
        ) : (
          <span className="image-panel-placeholder">
            {image.format} ({Math.ceil((image.data.length * 3) / 4)} bytes) — {t("no browser decoder")}
          </span>
        )}
      </div>
    </div>
  );
}
