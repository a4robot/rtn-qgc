import { beforeEach, expect, test } from "bun:test";

import type { LinkConfig, SettingValue } from "../bridge/types.ts";
import { settingKey, useSettingsStore } from "./settingsStore.ts";

const initial = useSettingsStore.getInitialState();

function makeSetting(overrides: Partial<SettingValue> = {}): SettingValue {
  return {
    group: "Video",
    name: "videoSource",
    value: "RTSP Video Stream",
    meta: { type: "string", enumValues: ["Video Stream Disabled", "RTSP Video Stream", "UDP h.264 Video Stream"] },
    ...overrides,
  };
}

function makeLink(overrides: Partial<LinkConfig> = {}): LinkConfig {
  return {
    name: "UDP Link (AutoConnect)",
    type: "udp",
    connected: false,
    autoConnect: true,
    config: { listenPort: 14550, targetHosts: [] },
    ...overrides,
  };
}

beforeEach(() => {
  useSettingsStore.setState(initial, true);
});

test("settingKey scopes uniqueness to (group, name)", () => {
  expect(settingKey("Video", "videoSource")).toBe("Video.videoSource");
});

test("applySettings upserts into the settings map, keyed by group.name", () => {
  const store = useSettingsStore.getState();
  store.applySettings([
    makeSetting(),
    makeSetting({ group: "AutoConnect", name: "autoConnectUDP", value: true, meta: { type: "bool" } }),
  ]);
  const settings = useSettingsStore.getState().settings;
  expect(settings["Video.videoSource"]?.value).toBe("RTSP Video Stream");
  expect(settings["AutoConnect.autoConnectUDP"]?.value).toBe(true);
});

test("applySettings on a scoped group snapshot does not wipe other groups (upsert, not replace)", () => {
  const store = useSettingsStore.getState();
  store.applySettings([makeSetting()]);
  store.applySettings([
    { group: "AutoConnect", name: "autoConnectUDP", value: true, meta: { type: "bool" } },
  ]);
  const settings = useSettingsStore.getState().settings;
  expect(settings["Video.videoSource"]?.value).toBe("RTSP Video Stream"); // still present
  expect(settings["AutoConnect.autoConnectUDP"]?.value).toBe(true);
});

test("applySettings (the setSetting response shape: one entry) clears that key's pending flag", () => {
  const store = useSettingsStore.getState();
  store.markSettingPending("Video", "videoSource");
  store.applySettings([makeSetting({ value: "UDP h.264 Video Stream" })]);
  expect(useSettingsStore.getState().pendingSettingKeys["Video.videoSource"]).toBeUndefined();
});

test("applySettingChanged (settingChanged push, §16.5) applies value+meta and clears pending", () => {
  const store = useSettingsStore.getState();
  store.markSettingPending("Video", "streamEnabled");
  store.applySettingChanged("Video", "streamEnabled", true, { type: "bool" });
  const setting = useSettingsStore.getState().settings["Video.streamEnabled"];
  expect(setting?.value).toBe(true);
  expect(setting?.meta.type).toBe("bool");
  expect(useSettingsStore.getState().pendingSettingKeys["Video.streamEnabled"]).toBeUndefined();
});

test("markSettingPending / clearSettingPending track one key independently of others", () => {
  const store = useSettingsStore.getState();
  store.markSettingPending("Video", "videoSource");
  expect(useSettingsStore.getState().pendingSettingKeys["Video.videoSource"]).toBe(true);
  expect(useSettingsStore.getState().pendingSettingKeys["Video.udpUrl"]).toBeUndefined();
  store.clearSettingPending("Video", "videoSource");
  expect(useSettingsStore.getState().pendingSettingKeys["Video.videoSource"]).toBeUndefined();
});

test("applyLinksSnapshot replaces the whole links list", () => {
  const store = useSettingsStore.getState();
  store.applyLinksSnapshot([
    makeLink(),
    makeLink({ name: "my-serial-link", type: "serial", config: { port: "/dev/ttyUSB0", baud: 57600 } }),
  ]);
  expect(useSettingsStore.getState().links).toHaveLength(2);
});

test("applyLinksSnapshot overwrites a prior snapshot wholesale (state-not-events)", () => {
  const store = useSettingsStore.getState();
  store.applyLinksSnapshot([makeLink()]);
  store.applyLinksSnapshot([makeLink({ name: "my-tcp-link", type: "tcp", config: { host: "127.0.0.1", port: 5760 } })]);
  const links = useSettingsStore.getState().links;
  expect(links).toHaveLength(1);
  expect(links[0]?.name).toBe("my-tcp-link");
});

test("removeLinkLocal drops the link by name and clears its pending op", () => {
  const store = useSettingsStore.getState();
  store.applyLinksSnapshot([makeLink()]);
  store.markLinkPending("UDP Link (AutoConnect)", "remove");
  store.removeLinkLocal("UDP Link (AutoConnect)");
  expect(useSettingsStore.getState().links).toEqual([]);
  expect(useSettingsStore.getState().pendingLinkOps["UDP Link (AutoConnect)"]).toBeUndefined();
});

test("markLinkPending / clearLinkPending track per-link op independently, keyed by name", () => {
  const store = useSettingsStore.getState();
  store.markLinkPending("link-a", "connect");
  store.markLinkPending("link-b", "disconnect");
  expect(useSettingsStore.getState().pendingLinkOps).toEqual({ "link-a": "connect", "link-b": "disconnect" });
  store.clearLinkPending("link-a");
  expect(useSettingsStore.getState().pendingLinkOps).toEqual({ "link-b": "disconnect" });
});

test("clear drops all settings and links state", () => {
  const store = useSettingsStore.getState();
  store.applySettings([makeSetting()]);
  store.applyLinksSnapshot([makeLink()]);
  store.markSettingPending("Video", "videoSource");
  store.markLinkPending("link-a", "connect");
  store.clear();
  const state = useSettingsStore.getState();
  expect(state.settings).toEqual({});
  expect(state.links).toEqual([]);
  expect(state.pendingSettingKeys).toEqual({});
  expect(state.pendingLinkOps).toEqual({});
});
