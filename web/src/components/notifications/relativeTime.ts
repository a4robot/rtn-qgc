/**
 * Pure relative-time formatting for the notification history dropdown —
 * extracted from NotificationBell so the bucketing logic is unit-testable
 * without React/DOM (mirrors uiStore's sidePanelSync.ts pattern).
 */

export function formatRelativeTime(
  receivedAtMs: number,
  t: (key: string, options?: any) => string,
  nowMs: number = Date.now(),
): string {
  const deltaS = Math.max(0, Math.round((nowMs - receivedAtMs) / 1000));
  if (deltaS < 5) {
    return t("just now");
  }
  if (deltaS < 60) {
    return t("{{count}}s ago", { count: deltaS });
  }
  const deltaMin = Math.floor(deltaS / 60);
  if (deltaMin < 60) {
    return t("{{count}}m ago", { count: deltaMin });
  }
  const deltaHr = Math.floor(deltaMin / 60);
  if (deltaHr < 24) {
    return t("{{count}}h ago", { count: deltaHr });
  }
  const deltaDay = Math.floor(deltaHr / 24);
  return t("{{count}}d ago", { count: deltaDay });
}
