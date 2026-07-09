/**
 * Pure relative-time formatting for the notification history dropdown —
 * extracted from NotificationBell so the bucketing logic is unit-testable
 * without React/DOM (mirrors uiStore's sidePanelSync.ts pattern).
 */

export function formatRelativeTime(receivedAtMs: number, nowMs: number = Date.now()): string {
  const deltaS = Math.max(0, Math.round((nowMs - receivedAtMs) / 1000));
  if (deltaS < 5) {
    return "just now";
  }
  if (deltaS < 60) {
    return `${deltaS}s ago`;
  }
  const deltaMin = Math.floor(deltaS / 60);
  if (deltaMin < 60) {
    return `${deltaMin}m ago`;
  }
  const deltaHr = Math.floor(deltaMin / 60);
  if (deltaHr < 24) {
    return `${deltaHr}h ago`;
  }
  const deltaDay = Math.floor(deltaHr / 24);
  return `${deltaDay}d ago`;
}
