import { beforeEach, expect, test } from "bun:test";

import type { ImageState } from "../bridge/types.ts";
import { useImageStore } from "./imageStore.ts";

const initialImages = useImageStore.getInitialState();

function makeImage(overrides: Partial<ImageState> = {}): ImageState {
  return {
    channel: "image",
    seq: 1,
    snapshot: true,
    vehicleId: 1,
    imageIndex: 1,
    format: "jpeg",
    width: 8,
    height: 8,
    data: "ZGF0YQ==", // "data"
    ...overrides,
  } as ImageState;
}

beforeEach(() => {
  useImageStore.setState(initialImages, true);
});

test("applyImage stores the image keyed by vehicleId", () => {
  useImageStore.getState().applyImage(makeImage(), 123);
  const image = useImageStore.getState().images[1];
  expect(image?.imageIndex).toBe(1);
  expect(image?.format).toBe("jpeg");
  expect(image?.width).toBe(8);
  expect(image?.height).toBe(8);
  expect(image?.data).toBe("ZGF0YQ==");
  expect(image?.lastUpdateAtMs).toBe(123);
  expect(image?.count).toBe(1);
});

test("count increments across successive applies for the same vehicle", () => {
  const store = useImageStore.getState();
  store.applyImage(makeImage({ imageIndex: 1 }));
  store.applyImage(makeImage({ imageIndex: 2, data: "bmV3" }));
  store.applyImage(makeImage({ imageIndex: 3, data: "bmV3Mg==" }));
  const image = useImageStore.getState().images[1];
  expect(image?.imageIndex).toBe(3);
  expect(image?.data).toBe("bmV3Mg==");
  expect(image?.count).toBe(3);
});

test("images for different vehicles don't clobber each other", () => {
  const store = useImageStore.getState();
  store.applyImage(makeImage({ vehicleId: 1, imageIndex: 5 }));
  store.applyImage(makeImage({ vehicleId: 2, imageIndex: 9 }));
  const images = useImageStore.getState().images;
  expect(images[1]?.imageIndex).toBe(5);
  expect(images[2]?.imageIndex).toBe(9);
  expect(images[1]?.count).toBe(1);
  expect(images[2]?.count).toBe(1);
});

test("a raw8u image round-trips width/height/format unchanged", () => {
  useImageStore.getState().applyImage(makeImage({ format: "raw8u", width: 4, height: 4, data: "AAECAw==" }));
  const image = useImageStore.getState().images[1];
  expect(image?.format).toBe("raw8u");
  expect(image?.width).toBe(4);
  expect(image?.height).toBe(4);
});

test("clear drops all image state", () => {
  const store = useImageStore.getState();
  store.applyImage(makeImage());
  store.clear();
  expect(useImageStore.getState().images).toEqual({});
});

test("useImage/useImages selectors return undefined / empty before any image", () => {
  expect(useImageStore.getState().images[1]).toBeUndefined();
  expect(useImageStore.getState().images).toEqual({});
});
