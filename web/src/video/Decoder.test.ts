import { describe, expect, test } from "bun:test";

import {
  DEFAULT_H264_CODEC,
  VIDEO_FRAME_HEADER_BYTES,
  VIDEO_FRAME_MAGIC,
  VIDEO_FRAME_VERSION,
  codecStringFromAnnexB,
  parseVideoFrame,
} from "./Decoder.ts";

/** Build a binary video frame per PROTOCOL.md §9.2 (little-endian header). */
function makeFrame(options: {
  magic?: number;
  version?: number;
  streamId?: number;
  flags?: number;
  timestampUs?: bigint;
  payload?: Uint8Array;
}): Uint8Array {
  const payload = options.payload ?? new Uint8Array([0, 0, 0, 1, 0x65, 0xaa]);
  const bytes = new Uint8Array(VIDEO_FRAME_HEADER_BYTES + payload.length);
  const view = new DataView(bytes.buffer);
  view.setUint16(0, options.magic ?? VIDEO_FRAME_MAGIC, true);
  view.setUint8(2, options.version ?? VIDEO_FRAME_VERSION);
  view.setUint8(3, options.streamId ?? 0);
  view.setUint8(4, options.flags ?? 0);
  view.setBigUint64(8, options.timestampUs ?? 0n, true);
  bytes.set(payload, VIDEO_FRAME_HEADER_BYTES);
  return bytes;
}

describe("parseVideoFrame", () => {
  test("parses a valid keyframe header", () => {
    const payload = new Uint8Array([0, 0, 0, 1, 0x65, 1, 2, 3]);
    const frame = makeFrame({
      streamId: 3,
      flags: 0x01,
      timestampUs: 1_767_690_000_123_456n,
      payload,
    });

    const result = parseVideoFrame(frame);
    expect(result.ok).toBe(true);
    if (!result.ok) throw new Error("unreachable");
    expect(result.header.streamId).toBe(3);
    expect(result.header.keyframe).toBe(true);
    expect(result.header.timestampUs).toBe(1_767_690_000_123_456n);
    expect(Array.from(result.header.payload)).toEqual(Array.from(payload));
  });

  test("keyframe flag is bit 0 only; reserved bits are ignored", () => {
    const delta = parseVideoFrame(makeFrame({ flags: 0x00 }));
    const reservedOnly = parseVideoFrame(makeFrame({ flags: 0xfe }));
    const keyWithReserved = parseVideoFrame(makeFrame({ flags: 0xff }));
    if (!delta.ok || !reservedOnly.ok || !keyWithReserved.ok) {
      throw new Error("unreachable");
    }
    expect(delta.header.keyframe).toBe(false);
    expect(reservedOnly.header.keyframe).toBe(false);
    expect(keyWithReserved.header.keyframe).toBe(true);
  });

  test("magic is little-endian: bytes on the wire are 0x56 0x46", () => {
    const frame = makeFrame({});
    expect(frame[0]).toBe(0x56);
    expect(frame[1]).toBe(0x46);
    // Same bytes big-endian would be 0x5646 — must be rejected.
    const swapped = makeFrame({ magic: 0x5646 });
    expect(parseVideoFrame(swapped)).toEqual({ ok: false, error: "badMagic" });
  });

  test("rejects bad magic", () => {
    const result = parseVideoFrame(makeFrame({ magic: 0x1234 }));
    expect(result).toEqual({ ok: false, error: "badMagic" });
  });

  test("rejects unknown version", () => {
    const result = parseVideoFrame(makeFrame({ version: 2 }));
    expect(result).toEqual({ ok: false, error: "badVersion" });
  });

  test("rejects frames shorter than the 16-byte header", () => {
    const result = parseVideoFrame(new Uint8Array(VIDEO_FRAME_HEADER_BYTES - 1));
    expect(result).toEqual({ ok: false, error: "tooShort" });
  });

  test("accepts a header-only frame (empty payload)", () => {
    const result = parseVideoFrame(makeFrame({ payload: new Uint8Array(0) }));
    if (!result.ok) throw new Error("unreachable");
    expect(result.header.payload.length).toBe(0);
  });

  test("payload is a zero-copy view into the input buffer", () => {
    const frame = makeFrame({});
    const result = parseVideoFrame(frame);
    if (!result.ok) throw new Error("unreachable");
    expect(result.header.payload.buffer).toBe(frame.buffer);
    expect(result.header.payload.byteOffset).toBe(VIDEO_FRAME_HEADER_BYTES);
  });

  test("accepts an ArrayBuffer (as delivered by a binary WS message)", () => {
    const frame = makeFrame({ streamId: 7 });
    // Copy into a standalone ArrayBuffer, as WebSocket binaryType gives us.
    const buffer = frame.slice().buffer;
    const result = parseVideoFrame(buffer);
    if (!result.ok) throw new Error("unreachable");
    expect(result.header.streamId).toBe(7);
  });

  test("timestampUs uses the full u64 range", () => {
    const big = 0xffff_ffff_ffff_ffffn;
    const result = parseVideoFrame(makeFrame({ timestampUs: big }));
    if (!result.ok) throw new Error("unreachable");
    expect(result.header.timestampUs).toBe(big);
  });
});

describe("codecStringFromAnnexB", () => {
  // SPS NAL: header 0x67 (type 7), then profile_idc / constraints / level_idc.
  const sps = new Uint8Array([0x67, 0x64, 0x00, 0x28, 0xac, 0xd9]);

  test("derives avc1.PPCCLL from an SPS after a 4-byte start code", () => {
    const au = new Uint8Array([0, 0, 0, 1, ...sps]);
    expect(codecStringFromAnnexB(au)).toBe("avc1.640028");
  });

  test("derives from an SPS after a 3-byte start code", () => {
    const au = new Uint8Array([0, 0, 1, ...sps]);
    expect(codecStringFromAnnexB(au)).toBe("avc1.640028");
  });

  test("skips non-SPS NAL units to find the SPS", () => {
    const au = new Uint8Array([
      0, 0, 0, 1, 0x09, 0xf0, // AUD (type 9)
      0, 0, 0, 1, ...sps, // SPS (type 7)
      0, 0, 0, 1, 0x68, 0xeb, // PPS (type 8)
      0, 0, 0, 1, 0x65, 0x88, // IDR slice (type 5)
    ]);
    expect(codecStringFromAnnexB(au)).toBe("avc1.640028");
  });

  test("matches the DEFAULT_H264_CODEC style (baseline SPS)", () => {
    const au = new Uint8Array([0, 0, 0, 1, 0x67, 0x42, 0xe0, 0x1e, 0xda]);
    expect(codecStringFromAnnexB(au)).toBe(DEFAULT_H264_CODEC);
  });

  test("returns null when no SPS is present", () => {
    const au = new Uint8Array([0, 0, 0, 1, 0x65, 0x88, 0x84]); // IDR only
    expect(codecStringFromAnnexB(au)).toBeNull();
  });

  test("returns null on a truncated SPS", () => {
    const au = new Uint8Array([0, 0, 0, 1, 0x67, 0x64]); // SPS cut short
    expect(codecStringFromAnnexB(au)).toBeNull();
  });

  test("returns null on empty input", () => {
    expect(codecStringFromAnnexB(new Uint8Array(0))).toBeNull();
  });
});
