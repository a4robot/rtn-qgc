/**
 * Settings store — the whitelisted ghost-configuration settings (PROTOCOL.md
 * §16.2: Video group, App group, AutoConnect flags) plus the configured
 * link list (§16.7), and a small "pending" index so the UI can show
 * optimistic-off feedback: a `setSetting`/link request marks its key
 * pending, and only `settingChanged` (§16.5, the authoritative value-applied
 * signal per its own doc comment in bridge/types.ts) or a fresh `getLinks`
 * clears it — the store never guesses a new value itself.
 *
 * Settings are keyed by `"<group>.<name>"` (see {@link settingKey}) since
 * §16.2 scopes uniqueness to the (group, name) pair, not name alone.
 * Applying a `settingChanged` push (which DOES carry {@link SettingMeta},
 * unlike this store's original pre-§16-landing guess) always has full
 * value+meta, so no meta-merging heuristic is needed here.
 *
 * Links are keyed by `name` (§16.7: "the primary key" — there is no
 * server-assigned id) and are always applied as a full-list replace
 * (`applyLinksSnapshot`), since `linkAck` never carries the resulting link
 * state (§16.7: only `{status, name, reason?}`) — the UI must re-fetch
 * `getLinks` to observe the effect of an add/remove/connect/disconnect.
 */

import { create } from "zustand";

import type { LinkConfig, SettingGroup, SettingMeta, SettingScalar, SettingValue } from "../bridge/types.ts";

/** Key settings are stored under: PROTOCOL.md §16.2 scopes uniqueness to (group, name). */
export function settingKey(group: string, name: string): string {
  return `${group}.${name}`;
}

/**
 * In-flight link operation, tracked so buttons can show a busy state per
 * link. `addLink` isn't here — a not-yet-created link has no `name` key to
 * track by until its `linkAck` lands, so "adding" pending state is local
 * component state in SettingsPanel.tsx instead (mirrors ParamEdit's local
 * `pending`, which likewise has no natural store key before a response).
 */
export type PendingLinkOp = "connect" | "disconnect" | "remove";

export interface SettingsStoreState {
  /** Latest known value+meta per whitelisted setting, keyed by {@link settingKey}. */
  settings: Record<string, SettingValue>;
  /** Setting keys with a `setSetting` request awaiting settingChanged confirmation. */
  pendingSettingKeys: Record<string, true>;
  /** Latest known configured links (full snapshot from the last `getLinks`). */
  links: LinkConfig[];
  /** Links with a connect/disconnect/remove request in flight, keyed by link `name`. */
  pendingLinkOps: Record<string, PendingLinkOp>;

  /** Merge settings into the map (response to `getSettings` — full or one `group` — or `setSetting`'s one-entry response). Upsert, not replace: a scoped `getSettings(group)` must not wipe other groups. */
  applySettings: (settings: SettingValue[]) => void;
  /** Apply one `settingChanged` push (§16.5 — always carries full value+meta). */
  applySettingChanged: (group: SettingGroup, name: string, value: SettingScalar, meta: SettingMeta) => void;
  markSettingPending: (group: string, name: string) => void;
  clearSettingPending: (group: string, name: string) => void;

  /** Replace the whole links list (response to `getLinks`, §16.7). */
  applyLinksSnapshot: (links: LinkConfig[]) => void;
  /** Drop one link locally by name (instant UX for an accepted `removeLink`, ahead of any re-fetch). */
  removeLinkLocal: (name: string) => void;
  markLinkPending: (name: string, op: PendingLinkOp) => void;
  clearLinkPending: (name: string) => void;

  clear: () => void;
}

function withoutKey<T>(record: Record<string, T>, key: string): Record<string, T> {
  if (!(key in record)) {
    return record;
  }
  const next = { ...record };
  delete next[key];
  return next;
}

export const useSettingsStore = create<SettingsStoreState>()((set) => ({
  settings: {},
  pendingSettingKeys: {},
  links: [],
  pendingLinkOps: {},

  applySettings: (settings) =>
    set((prev) => {
      const next = { ...prev.settings };
      let pending = prev.pendingSettingKeys;
      for (const setting of settings) {
        const key = settingKey(setting.group, setting.name);
        next[key] = setting;
        pending = withoutKey(pending, key);
      }
      return { settings: next, pendingSettingKeys: pending };
    }),

  applySettingChanged: (group, name, value, meta) =>
    set((prev) => {
      const key = settingKey(group, name);
      return {
        settings: { ...prev.settings, [key]: { group, name, value, meta } },
        pendingSettingKeys: withoutKey(prev.pendingSettingKeys, key),
      };
    }),

  markSettingPending: (group, name) =>
    set((prev) => ({
      pendingSettingKeys: { ...prev.pendingSettingKeys, [settingKey(group, name)]: true },
    })),

  clearSettingPending: (group, name) =>
    set((prev) => ({
      pendingSettingKeys: withoutKey(prev.pendingSettingKeys, settingKey(group, name)),
    })),

  applyLinksSnapshot: (links) => set({ links }),

  removeLinkLocal: (name) =>
    set((prev) => ({
      links: prev.links.filter((l) => l.name !== name),
      pendingLinkOps: withoutKey(prev.pendingLinkOps, name),
    })),

  markLinkPending: (name, op) =>
    set((prev) => ({ pendingLinkOps: { ...prev.pendingLinkOps, [name]: op } })),

  clearLinkPending: (name) =>
    set((prev) => ({ pendingLinkOps: withoutKey(prev.pendingLinkOps, name) })),

  clear: () => set({ settings: {}, pendingSettingKeys: {}, links: [], pendingLinkOps: {} }),
}));

/** All known settings, keyed by {@link settingKey}. */
export function useSettings(): Record<string, SettingValue> {
  return useSettingsStore((state) => state.settings);
}

/** One setting's current value+meta, or undefined if never seen. */
export function useSetting(group: string, name: string): SettingValue | undefined {
  return useSettingsStore((state) => state.settings[settingKey(group, name)]);
}

/** Whether `group.name` has a `setSetting` request awaiting confirmation. */
export function useSettingPending(group: string, name: string): boolean {
  return useSettingsStore((state) => Boolean(state.pendingSettingKeys[settingKey(group, name)]));
}

/** All configured links. */
export function useLinks(): LinkConfig[] {
  return useSettingsStore((state) => state.links);
}

/** In-flight op for one link (by name), or undefined if none. */
export function useLinkPending(name: string): PendingLinkOp | undefined {
  return useSettingsStore((state) => state.pendingLinkOps[name]);
}
