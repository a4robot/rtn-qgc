/**
 * Notification store — ring buffer of the last N notification pushes
 * (PROTOCOL.md §14, server-push, no subscription/channel — see
 * bridge/types.ts's {@link Notification}).
 *
 * Unlike vehicleStore/missionStore (state-not-events, latest-wins per
 * vehicle), notifications ARE events: each push is appended, not merged, and
 * the store caps at MAX_NOTIFICATIONS (oldest dropped) so a chatty bridge
 * can't grow this unbounded. `notifications` is kept most-recent-first so UI
 * consumers (toasts, the header dropdown history) can render it directly.
 */

import { create } from "zustand";

import type { Notification, NotificationSeverity } from "../bridge/types.ts";
import { loadSpeechMuted, saveSpeechMuted } from "../components/notifications/speech.ts";

/** Ring buffer capacity. */
export const MAX_NOTIFICATIONS = 50;

/** Renderable notification: the wire payload plus a local id and receipt time. */
export interface NotificationRecord {
  id: string;
  severity: NotificationSeverity;
  text: string;
  timeUs: number;
  vehicleId?: number;
  /** Local receipt time, ms since epoch — drives toast auto-dismiss and relative timestamps. */
  receivedAtMs: number;
}

export interface NotificationStoreState {
  /** Most recent first, capped at {@link MAX_NOTIFICATIONS}. */
  notifications: NotificationRecord[];
  /** Count of notifications appended since the last {@link markAllRead}. */
  unreadCount: number;
  /**
   * Whether warning/critical notifications are spoken aloud (see
   * components/notifications/speech.ts). Seeded from localStorage so the
   * preference survives reloads; toggling persists it back.
   */
  speechMuted: boolean;

  addNotification: (notification: Notification, receivedAtMs?: number) => void;
  markAllRead: () => void;
  toggleSpeechMuted: () => void;
  clear: () => void;
}

/**
 * The bridge doesn't assign notifications an id (no request/response
 * lifecycle to key by, unlike command/mission) — this store mints a local
 * one so React lists and toast dismissal have a stable key.
 */
function nextId(): string {
  if (typeof crypto !== 'undefined' && crypto.randomUUID) {
    return `notif-${crypto.randomUUID()}`;
  }
  return `notif-${Math.random().toString(36).substring(2, 11)}-${Date.now()}`;
}

export const useNotificationStore = create<NotificationStoreState>()((set, get) => ({
  notifications: [],
  unreadCount: 0,
  speechMuted: loadSpeechMuted(),

  addNotification: (notification, receivedAtMs = Date.now()) =>
    set((prev) => {
      const record: NotificationRecord = {
        id: nextId(),
        severity: notification.severity,
        text: notification.text,
        timeUs: notification.timeUs,
        ...(notification.vehicleId !== undefined ? { vehicleId: notification.vehicleId } : {}),
        receivedAtMs,
      };
      return {
        notifications: [record, ...prev.notifications].slice(0, MAX_NOTIFICATIONS),
        unreadCount: prev.unreadCount + 1,
      };
    }),

  markAllRead: () => set({ unreadCount: 0 }),

  toggleSpeechMuted: () => {
    const next = !get().speechMuted;
    saveSpeechMuted(next);
    set({ speechMuted: next });
  },

  clear: () => set({ notifications: [], unreadCount: 0 }),
}));

/** All buffered notifications, most recent first. */
export function useNotifications(): NotificationRecord[] {
  return useNotificationStore((state) => state.notifications);
}

/** Count of notifications since the last {@link NotificationStoreState.markAllRead}. */
export function useUnreadNotificationCount(): number {
  return useNotificationStore((state) => state.unreadCount);
}

/** Whether warning/critical notifications are currently spoken aloud. */
export function useSpeechMuted(): boolean {
  return useNotificationStore((state) => state.speechMuted);
}
