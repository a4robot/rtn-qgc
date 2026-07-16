/**
 * Stacked toast pop-ups for notification pushes (PROTOCOL.md §14, no
 * subscription — see bridge/types.ts's `Notification`). Only notifications
 * that arrive *after* this component mounts become toasts (the header bell
 * dropdown is where the full history, including anything buffered before
 * mount, lives) — otherwise a page reload would replay up to 50 old toasts.
 *
 * Auto-dismiss timing per severity: info 5s, warning 10s, critical sticky
 * (stays until clicked). Warning/critical are also spoken via
 * window.speechSynthesis unless muted (see ./speech.ts) — this is the one
 * place that watches every fresh notification exactly once, so it owns both
 * the toast-visibility and the speech side effect.
 */

import { useEffect, useRef, useState } from "react";

import { useNotifications, useNotificationStore } from "../../store/index.ts";
import { speak, SPOKEN_SEVERITIES } from "./speech.ts";
import { useTranslation } from "react-i18next";
import "./notifications.css";

/** ms until a toast of this severity auto-dismisses; null = sticky (manual dismiss only). */
const AUTO_DISMISS_MS: Record<string, number | null> = {
  info: 5000,
  warning: 10000,
  critical: null,
};

export function NotificationToasts() {
  const { t } = useTranslation();
  const notifications = useNotifications();
  const speechMuted = useNotificationStore((s) => s.speechMuted);
  const speechMutedRef = useRef(speechMuted);
  speechMutedRef.current = speechMuted;

  const [visibleIds, setVisibleIds] = useState<string[]>([]);
  const seenIds = useRef<Set<string>>(new Set());
  const mounted = useRef(false);
  const timers = useRef(new Map<string, ReturnType<typeof setTimeout>>());

  const dismiss = (id: string): void => {
    const timer = timers.current.get(id);
    if (timer !== undefined) {
      clearTimeout(timer);
      timers.current.delete(id);
    }
    setVisibleIds((prev) => prev.filter((existing) => existing !== id));
  };

  useEffect(() => {
    // First run: whatever is already buffered predates this mount — mark it
    // seen (so it never toasts) without touching visibility or speech.
    if (!mounted.current) {
      mounted.current = true;
      for (const n of notifications) {
        seenIds.current.add(n.id);
      }
      return;
    }

    const fresh = notifications.filter((n) => !seenIds.current.has(n.id));
    if (fresh.length === 0) {
      return;
    }
    for (const n of fresh) {
      seenIds.current.add(n.id);
    }
    // Store order is already most-recent-first; prepend so new toasts stack on top.
    setVisibleIds((prev) => [...fresh.map((n) => n.id), ...prev]);

    for (const n of fresh) {
      const autoDismissMs = AUTO_DISMISS_MS[n.severity] ?? 5000;
      if (autoDismissMs !== null) {
        const timer = setTimeout(() => dismiss(n.id), autoDismissMs);
        timers.current.set(n.id, timer);
      }
      if (SPOKEN_SEVERITIES.has(n.severity) && !speechMutedRef.current) {
        speak(t(n.text));
      }
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [notifications]);

  // Clear every pending timer on unmount.
  useEffect(() => {
    const timerMap = timers.current;
    return () => {
      for (const timer of timerMap.values()) {
        clearTimeout(timer);
      }
      timerMap.clear();
    };
  }, []);

  const byId = new Map(notifications.map((n) => [n.id, n]));
  const toasts = visibleIds.map((id) => byId.get(id)).filter((n) => n !== undefined);

  if (toasts.length === 0) {
    return null;
  }

  return (
    <div className="notif-toast-stack" aria-live="polite" aria-label="Notifications">
      {toasts.map((n) => (
        <button
          key={n.id}
          type="button"
          className={`notif-toast notif-toast--${n.severity}`}
          onClick={() => dismiss(n.id)}
          aria-label={`Dismiss ${n.severity} notification: ${n.text}`}
        >
          <span className="notif-toast-severity">{t(n.severity)}</span>
          <span className="notif-toast-text">{t(n.text)}</span>
        </button>
      ))}
    </div>
  );
}
