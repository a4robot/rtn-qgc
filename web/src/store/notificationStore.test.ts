import { beforeEach, expect, test } from "bun:test";

import type { Notification } from "../bridge/types.ts";
import { MAX_NOTIFICATIONS, useNotificationStore } from "./notificationStore.ts";

const initialState = useNotificationStore.getInitialState();

function makeNotification(overrides: Partial<Notification> = {}): Notification {
  return {
    type: "notification",
    severity: "info",
    text: "mock bridge ready",
    timeUs: 1_767_690_005_000_000,
    ...overrides,
  };
}

beforeEach(() => {
  useNotificationStore.setState(initialState, true);
});

test("addNotification prepends a record with a generated id and receivedAtMs", () => {
  useNotificationStore.getState().addNotification(makeNotification(), 123);
  const notifications = useNotificationStore.getState().notifications;
  expect(notifications).toHaveLength(1);
  expect(notifications[0]?.text).toBe("mock bridge ready");
  expect(notifications[0]?.severity).toBe("info");
  expect(notifications[0]?.receivedAtMs).toBe(123);
  expect(typeof notifications[0]?.id).toBe("string");
  expect(notifications[0]?.id.length).toBeGreaterThan(0);
});

test("vehicleId is omitted when absent on the wire message, present when given", () => {
  useNotificationStore.getState().addNotification(makeNotification());
  expect(useNotificationStore.getState().notifications[0]?.vehicleId).toBeUndefined();

  useNotificationStore.getState().clear();
  useNotificationStore.getState().addNotification(makeNotification({ vehicleId: 3 }));
  expect(useNotificationStore.getState().notifications[0]?.vehicleId).toBe(3);
});

test("notifications are ordered most-recent-first", () => {
  const store = useNotificationStore.getState();
  store.addNotification(makeNotification({ text: "first" }), 1);
  store.addNotification(makeNotification({ text: "second" }), 2);
  store.addNotification(makeNotification({ text: "third" }), 3);
  const notifications = useNotificationStore.getState().notifications;
  expect(notifications.map((n) => n.text)).toEqual(["third", "second", "first"]);
});

test("ring buffer caps at MAX_NOTIFICATIONS, dropping the oldest", () => {
  const store = useNotificationStore.getState();
  for (let i = 0; i < MAX_NOTIFICATIONS + 10; i++) {
    store.addNotification(makeNotification({ text: `n${i}` }), i);
  }
  const notifications = useNotificationStore.getState().notifications;
  expect(notifications).toHaveLength(MAX_NOTIFICATIONS);
  // Most recent (highest i) first; oldest 10 dropped.
  expect(notifications[0]?.text).toBe(`n${MAX_NOTIFICATIONS + 9}`);
  expect(notifications.at(-1)?.text).toBe("n10");
});

test("unreadCount increments on each addNotification and resets on markAllRead", () => {
  const store = useNotificationStore.getState();
  expect(useNotificationStore.getState().unreadCount).toBe(0);
  store.addNotification(makeNotification());
  store.addNotification(makeNotification());
  expect(useNotificationStore.getState().unreadCount).toBe(2);
  store.markAllRead();
  expect(useNotificationStore.getState().unreadCount).toBe(0);
  // markAllRead doesn't drop the history, just the unread counter.
  expect(useNotificationStore.getState().notifications).toHaveLength(2);
});

test("markAllRead followed by a new push counts only the new one", () => {
  const store = useNotificationStore.getState();
  store.addNotification(makeNotification());
  store.markAllRead();
  store.addNotification(makeNotification());
  expect(useNotificationStore.getState().unreadCount).toBe(1);
});

test("clear drops both notifications and unreadCount", () => {
  const store = useNotificationStore.getState();
  store.addNotification(makeNotification());
  store.addNotification(makeNotification());
  store.clear();
  expect(useNotificationStore.getState().notifications).toEqual([]);
  expect(useNotificationStore.getState().unreadCount).toBe(0);
});

test("speechMuted defaults to false (no localStorage in the test environment)", () => {
  expect(useNotificationStore.getState().speechMuted).toBe(false);
});

test("toggleSpeechMuted flips speechMuted without touching notifications/unreadCount", () => {
  const store = useNotificationStore.getState();
  store.addNotification(makeNotification());
  expect(useNotificationStore.getState().speechMuted).toBe(false);

  store.toggleSpeechMuted();
  expect(useNotificationStore.getState().speechMuted).toBe(true);
  expect(useNotificationStore.getState().unreadCount).toBe(1);

  store.toggleSpeechMuted();
  expect(useNotificationStore.getState().speechMuted).toBe(false);
});

test("severity is preserved for warning and critical pushes", () => {
  const store = useNotificationStore.getState();
  store.addNotification(makeNotification({ severity: "warning", text: "w" }));
  store.addNotification(makeNotification({ severity: "critical", text: "c" }));
  const notifications = useNotificationStore.getState().notifications;
  expect(notifications[0]?.severity).toBe("critical");
  expect(notifications[1]?.severity).toBe("warning");
});
