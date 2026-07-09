/**
 * Header bell — unread badge + dropdown history (most recent first, relative
 * timestamps), plus a mute/unmute button for notification speech (§ see
 * ./speech.ts; preference persists in localStorage via the store).
 *
 * Presentation-only: reads notificationStore directly (small, header-local
 * control — same latitude VehicleSelect takes with connection/vehicleStore).
 */

import { useState } from "react";

import {
  useNotificationStore,
  useNotifications,
  useSpeechMuted,
  useUnreadNotificationCount,
} from "../../store/index.ts";
import { isSpeechSupported } from "./speech.ts";
import { formatRelativeTime } from "./relativeTime.ts";
import "./notifications.css";

export function NotificationBell() {
  const [open, setOpen] = useState(false);
  const notifications = useNotifications();
  const unreadCount = useUnreadNotificationCount();
  const speechMuted = useSpeechMuted();
  const markAllRead = useNotificationStore((s) => s.markAllRead);
  const toggleSpeechMuted = useNotificationStore((s) => s.toggleSpeechMuted);
  const speechSupported = isSpeechSupported();

  const toggleOpen = (): void => {
    // Side effect (markAllRead, a zustand `set`) must not live inside a
    // useState updater function — React may invoke that updater during
    // render (StrictMode double-invoke, or batched re-renders), and a set()
    // call there trips "Cannot update a component while rendering a
    // different component". Compute `next` from the current `open` (this
    // runs in the click handler, not during render) and call markAllRead
    // as a plain side effect afterward.
    const next = !open;
    setOpen(next);
    if (next) {
      markAllRead();
    }
  };

  return (
    <div className="notif-bell-group">
      {speechSupported && (
        <button
          type="button"
          className={`notif-mute-button${speechMuted ? " notif-mute-button--muted" : ""}`}
          onClick={toggleSpeechMuted}
          aria-pressed={speechMuted}
          aria-label={speechMuted ? "Unmute notification speech" : "Mute notification speech"}
          title={speechMuted ? "Notification speech muted" : "Notification speech on"}
        >
          {speechMuted ? "\u{1F507}" : "\u{1F50A}"}
        </button>
      )}

      <div className="notif-bell-wrap">
        <button
          type="button"
          className="notif-bell-button"
          onClick={toggleOpen}
          aria-expanded={open}
          aria-label={`Notifications${unreadCount > 0 ? ` (${unreadCount} unread)` : ""}`}
        >
          <span aria-hidden="true">{"\u{1F514}"}</span>
          {unreadCount > 0 && <span className="notif-bell-badge">{unreadCount > 99 ? "99+" : unreadCount}</span>}
        </button>

        {open && (
          <div className="notif-bell-dropdown" role="menu" aria-label="Notification history">
            {notifications.length === 0 ? (
              <div className="notif-bell-empty">no notifications yet</div>
            ) : (
              <ul className="notif-bell-list">
                {notifications.map((n) => (
                  <li key={n.id} className={`notif-bell-item notif-bell-item--${n.severity}`}>
                    <span className="notif-bell-item-severity">{n.severity}</span>
                    <span className="notif-bell-item-text">{n.text}</span>
                    <span className="notif-bell-item-time">{formatRelativeTime(n.receivedAtMs)}</span>
                  </li>
                ))}
              </ul>
            )}
          </div>
        )}
      </div>
    </div>
  );
}
