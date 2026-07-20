/**
 * Settings side-panel tab, PROTOCOL.md §16 — ghost configuration from the
 * cockpit instead of a config file: **Video** (§16.2's `Video` group —
 * source/url/enable/latency), **AutoConnect** (§16.2's `AutoConnect` flags),
 * **App** (§16.2's `App` group — offline-editing/mission-planning basics),
 * and **Links** (configured serial/udp/tcp connections + an Add Link form,
 * §16.7).
 *
 * Optimistic-off, same discipline as ParamEdit (§6.2)/WaypointList (§7.2):
 * every edit sends a request and shows "pending" until the bridge answers.
 * For settings, "answers" specifically means `settingChanged` (§16.5's
 * broadcast is the authoritative value-applied signal, fired for the
 * requester's own accepted write too) — `useSettingRequests` only clears its
 * *local* bookkeeping on the `settingsValue` response; the *store's* pending
 * flag is left for the settingChanged push (routed by bindBridge.ts) to
 * clear, so "Applying…" tracks the value actually landing, not just the
 * request being accepted.
 *
 * Links have no broadcast (§16.7: "v0.1 has no `linksChanged` broadcast") —
 * `connectLink`/`disconnectLink` are fire-and-forget, so `useLinkRequests`
 * re-polls `getLinks` after an accepted ack per the doc's own suggested
 * pattern ("a short poll loop settles quickly in practice").
 */

import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useTranslation } from "react-i18next";
import { loadVoiceLanguage, saveVoiceLanguage, loadVoiceSpeed, saveVoiceSpeed } from "../notifications/speech.ts";

import type { BridgeClient } from "../../bridge/BridgeClient.ts";
import type { AddableLinkType, LinkConfig, SettingGroup, SettingScalar } from "../../bridge/types.ts";
import {
  settingKey,
  useLinkPending,
  useLinks,
  useSetting,
  useSettingPending,
  useSettingsStore,
  useUiStore,
} from "../../store/index.ts";
import { validateLinkFields, type LinkFieldsInput } from "./validateLinkFields.ts";
import "./settings.css";

/** How long a setSetting request waits for a reply before giving up (defensive only — §16.1: writes are synchronous and never time out on a real bridge). */
const SETTING_TIMEOUT_MS = 5_000;
/** How long an addLink/removeLink/connectLink/disconnectLink request waits for its `linkAck`. */
const LINK_TIMEOUT_MS = 5_000;
/** Second `getLinks` poll after an accepted connect/disconnect, to catch the fire-and-forget transition (§16.7). */
const LINK_POLL_DELAY_MS = 700;

let requestSeq = 0;
function nextRequestId(prefix: string): string {
  requestSeq += 1;
  return `${prefix}-${Date.now()}-${requestSeq}`;
}

/** Fallback `videoSource` enum shown until the first getSettings snapshot answers (real list is sourced live server-side, §16.3). */
const DEFAULT_VIDEO_SOURCES = [
  "Video Stream Disabled",
  "RTSP Video Stream",
  "UDP h.264 Video Stream",
  "UDP h.265 Video Stream",
  "TCP-MPEG2 Video Stream",
  "MPEG-TS Video Stream",
];

/** Which URL field (if any) applies to a given `videoSource`, mirroring `GhostVideoSource::_resolveConfiguredUri()`'s per-source dispatch. */
function videoUrlFieldForSource(source: string): "udpUrl" | "rtspUrl" | "tcpUrl" | null {
  if (source.startsWith("UDP") || source === "MPEG-TS Video Stream") return "udpUrl";
  if (source === "RTSP Video Stream") return "rtspUrl";
  if (source === "TCP-MPEG2 Video Stream") return "tcpUrl";
  return null;
}

/**
 * Tracks in-flight `setSetting` requests keyed by request id -> (group,
 * name), so a `settingsError` (§10 rejection) or timeout can be routed back
 * to the right field — mirrors ParamEdit's `pendingIdRef`/`timeoutRef`
 * pattern, generalized to N concurrent fields since a settings form has
 * several. An accepted response only clears local tracking; the store's
 * pending flag waits for `settingChanged` (see this file's header comment).
 */
function useSettingRequests(client: BridgeClient) {
  const clearPending = useSettingsStore((state) => state.clearSettingPending);
  const markPending = useSettingsStore((state) => state.markSettingPending);
  const [errors, setErrors] = useState<Record<string, string>>({});
  const pendingRef = useRef(new Map<string, { group: SettingGroup; name: string }>());
  const timersRef = useRef(new Map<string, ReturnType<typeof setTimeout>>());

  useEffect(() => {
    return client.onSettingsResponse((message) => {
      const target = pendingRef.current.get(message.id);
      if (!target) {
        return; // response for a request we no longer track (timed out, or someone else's getSettings)
      }
      pendingRef.current.delete(message.id);
      const timer = timersRef.current.get(message.id);
      if (timer !== undefined) {
        clearTimeout(timer);
        timersRef.current.delete(message.id);
      }
      const key = settingKey(target.group, target.name);
      if (message.type === "settingsError") {
        clearPending(target.group, target.name);
        setErrors((prev) => ({ ...prev, [key]: message.message }));
        return;
      }
      // Accepted: leave the store's pending flag set — settingChanged (routed
      // via bindBridge) clears it when the value is actually applied.
      setErrors((prev) => {
        if (!(key in prev)) return prev;
        const { [key]: _dropped, ...rest } = prev;
        return rest;
      });
    });
  }, [client, clearPending]);

  useEffect(() => {
    return () => {
      for (const timer of timersRef.current.values()) {
        clearTimeout(timer);
      }
    };
  }, []);

  const setValue = useCallback(
    (group: SettingGroup, name: string, value: SettingScalar) => {
      const key = settingKey(group, name);
      const id = nextRequestId("set-setting");
      const sent = client.send({ type: "setSetting", id, group, name, value });
      if (!sent) {
        setErrors((prev) => ({ ...prev, [key]: "Not connected — unable to send" }));
        return;
      }
      pendingRef.current.set(id, { group, name });
      markPending(group, name);
      setErrors((prev) => {
        if (!(key in prev)) return prev;
        const { [key]: _dropped, ...rest } = prev;
        return rest;
      });
      const timer = setTimeout(() => {
        if (!pendingRef.current.has(id)) return; // already resolved
        pendingRef.current.delete(id);
        clearPending(group, name);
        setErrors((prev) => ({ ...prev, [key]: "No response from bridge — try again" }));
      }, SETTING_TIMEOUT_MS);
      timersRef.current.set(id, timer);
    },
    [client, markPending, clearPending],
  );

  return { errors, setValue };
}

interface SettingFieldProps {
  group: SettingGroup;
  name: string;
  label: string;
  error?: string;
  onChange: (value: SettingScalar) => void;
}

function VideoSourceField({ group, name, label, error, onChange }: SettingFieldProps) {
  const setting = useSetting(group, name);
  const pending = useSettingPending(group, name);
  const options = setting?.meta.enumValues ?? DEFAULT_VIDEO_SOURCES;
  const value = typeof setting?.value === "string" ? setting.value : DEFAULT_VIDEO_SOURCES[0]!;

  return (
    <div className="settings-field">
      <label htmlFor={`setting-${group}-${name}`}>{label}</label>
      <select
        id={`setting-${group}-${name}`}
        value={value}
        disabled={pending}
        onChange={(e) => onChange(e.target.value)}
      >
        {options.map((opt) => (
          <option key={opt} value={opt}>
            {opt}
          </option>
        ))}
      </select>
      {pending && <span className="settings-field-pending">Applying…</span>}
      {error && <span className="settings-field-error">{error}</span>}
    </div>
  );
}

function TextSettingField({ group, name, label, error, onChange }: SettingFieldProps) {
  const setting = useSetting(group, name);
  const pending = useSettingPending(group, name);
  const [text, setText] = useState(typeof setting?.value === "string" ? setting.value : "");
  const editingRef = useRef(false);

  // Sync from the store when a fresh value lands (snapshot or settingChanged),
  // but don't clobber an in-progress edit — tracked via focus rather than a
  // one-shot path-keyed effect (ParamEdit's approach) since this field
  // re-renders on every store update, not just on a path change.
  useEffect(() => {
    if (!editingRef.current && typeof setting?.value === "string") {
      setText(setting.value);
    }
  }, [setting?.value]);

  return (
    <div className="settings-field">
      <label htmlFor={`setting-${group}-${name}`}>{label}</label>
      <input
        id={`setting-${group}-${name}`}
        type="text"
        value={text}
        disabled={pending}
        onFocus={() => {
          editingRef.current = true;
        }}
        onChange={(e) => setText(e.target.value)}
        onBlur={() => {
          editingRef.current = false;
          if (text !== setting?.value) {
            onChange(text);
          }
        }}
      />
      {pending && <span className="settings-field-pending">Applying…</span>}
      {error && <span className="settings-field-error">{error}</span>}
    </div>
  );
}

/** Numeric setting field (`meta.type` `"int"`/`"double"`) — same edit-in-progress discipline as {@link TextSettingField}. */
function NumberSettingField({ group, name, label, error, onChange }: SettingFieldProps) {
  const setting = useSetting(group, name);
  const pending = useSettingPending(group, name);
  const [text, setText] = useState(typeof setting?.value === "number" ? String(setting.value) : "");
  const editingRef = useRef(false);

  useEffect(() => {
    if (!editingRef.current && typeof setting?.value === "number") {
      setText(String(setting.value));
    }
  }, [setting?.value]);

  return (
    <div className="settings-field">
      <label htmlFor={`setting-${group}-${name}`}>{label}</label>
      <input
        id={`setting-${group}-${name}`}
        type="number"
        step="any"
        value={text}
        disabled={pending}
        onFocus={() => {
          editingRef.current = true;
        }}
        onChange={(e) => setText(e.target.value)}
        onBlur={() => {
          editingRef.current = false;
          const parsed = Number(text);
          if (text.trim() !== "" && Number.isFinite(parsed) && parsed !== setting?.value) {
            onChange(parsed);
          }
        }}
      />
      {pending && <span className="settings-field-pending">Applying…</span>}
      {error && <span className="settings-field-error">{error}</span>}
    </div>
  );
}

function ToggleSettingField({ group, name, label, error, onChange }: SettingFieldProps) {
  const setting = useSetting(group, name);
  const pending = useSettingPending(group, name);
  const checked = setting?.value === true;

  return (
    <div className="settings-field settings-field--toggle">
      <label htmlFor={`setting-${group}-${name}`}>
        <input
          id={`setting-${group}-${name}`}
          type="checkbox"
          checked={checked}
          disabled={pending}
          onChange={(e) => onChange(e.target.checked)}
        />
        {label}
      </label>
      {pending && <span className="settings-field-pending">Applying…</span>}
      {error && <span className="settings-field-error">{error}</span>}
    </div>
  );
}

const VIDEO_URL_FIELD_LABELS: Record<"udpUrl" | "rtspUrl" | "tcpUrl", string> = {
  udpUrl: "UDP URL (host:port)",
  rtspUrl: "RTSP URL",
  tcpUrl: "TCP URL (host:port)",
};

function VideoSection({ client }: { client: BridgeClient }) {
  const { errors, setValue } = useSettingRequests(client);
  const { t } = useTranslation();
  const source = useSetting("Video", "videoSource");
  const sourceValue = typeof source?.value === "string" ? source.value : DEFAULT_VIDEO_SOURCES[0]!;
  const urlField = videoUrlFieldForSource(sourceValue);

  return (
    <section className="settings-section" aria-label="Video settings">
      <h3>{t("Video")}</h3>
      <VideoSourceField
        group="Video"
        name="videoSource"
        label={t("Source")}
        error={errors["Video.videoSource"]}
        onChange={(value) => setValue("Video", "videoSource", value)}
      />
      {urlField && (
        <TextSettingField
          group="Video"
          name={urlField}
          label={t(VIDEO_URL_FIELD_LABELS[urlField])}
          error={errors[`Video.${urlField}`]}
          onChange={(value) => setValue("Video", urlField, value)}
        />
      )}
      <ToggleSettingField
        group="Video"
        name="streamEnabled"
        label={t("Stream enabled")}
        error={errors["Video.streamEnabled"]}
        onChange={(value) => setValue("Video", "streamEnabled", value)}
      />
      <ToggleSettingField
        group="Video"
        name="lowLatencyMode"
        label={t("Low latency mode")}
        error={errors["Video.lowLatencyMode"]}
        onChange={(value) => setValue("Video", "lowLatencyMode", value)}
      />
    </section>
  );
}

const AUTO_CONNECT_TOGGLES: Array<{ name: string; label: string }> = [
  { name: "autoConnectUDP", label: "UDP" },
  { name: "autoConnectPixhawk", label: "Pixhawk (serial)" },
  { name: "autoConnectSiKRadio", label: "SiK Radio (serial)" },
  { name: "autoConnectRTKGPS", label: "RTK GPS (serial)" },
];

function AutoConnectSection({ client }: { client: BridgeClient }) {
  const { errors, setValue } = useSettingRequests(client);
  const { t } = useTranslation();

  return (
    <section className="settings-section" aria-label="AutoConnect settings">
      <h3>{t("AutoConnect")}</h3>
      {AUTO_CONNECT_TOGGLES.map(({ name, label }) => (
        <ToggleSettingField
          key={name}
          group="AutoConnect"
          name={name}
          label={t(label)}
          error={errors[`AutoConnect.${name}`]}
          onChange={(value) => setValue("AutoConnect", name, value)}
        />
      ))}
      <NumberSettingField
        group="AutoConnect"
        name="udpListenPort"
        label={t("UDP listen port")}
        error={errors["AutoConnect.udpListenPort"]}
        onChange={(value) => setValue("AutoConnect", "udpListenPort", value)}
      />
    </section>
  );
}

const APP_NUMBER_FIELDS: Array<{ name: string; label: string }> = [
  { name: "defaultMissionItemAltitude", label: "Default mission item altitude (m)" },
  { name: "offlineEditingCruiseSpeed", label: "Offline cruise speed (m/s)" },
  { name: "offlineEditingHoverSpeed", label: "Offline hover speed (m/s)" },
  { name: "offlineEditingAscentSpeed", label: "Offline ascent speed (m/s)" },
  { name: "offlineEditingDescentSpeed", label: "Offline descent speed (m/s)" },
];

function AppSection({ client }: { client: BridgeClient }) {
  const { errors, setValue } = useSettingRequests(client);
  const { t, i18n } = useTranslation();
  const [voiceLang, setVoiceLang] = useState(() => loadVoiceLanguage());
  const [voiceSpeed, setVoiceSpeed] = useState(() => loadVoiceSpeed());
  const vehicleMarkerStyle = useUiStore((state) => state.vehicleMarkerStyle);
  const setVehicleMarkerStyle = useUiStore((state) => state.setVehicleMarkerStyle);
  const mapStyle = useUiStore((state) => state.mapStyle);
  const setMapStyle = useUiStore((state) => state.setMapStyle);

  const handleVoiceLangChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
    const val = e.target.value;
    setVoiceLang(val);
    saveVoiceLanguage(val);
  };

  const handleVoiceSpeedChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
    const val = parseFloat(e.target.value);
    setVoiceSpeed(val);
    saveVoiceSpeed(val);
  };

  return (
    <section className="settings-section" aria-label="App settings">
      <h3>{t("App settings")}</h3>
      
      <div className="settings-field">
        <div className="settings-field-label">{t("UI Language")}</div>
        <div className="settings-field-control">
          <select 
            className="settings-field-input"
            value={i18n.language?.split('-')[0] || "en"}
            onChange={(e) => {
              const val = e.target.value;
              i18n.changeLanguage(val);
              setValue("App", "qLocaleLanguage", val === "th" ? 112 : 1);
            }}
          >
            <option value="en">{t("English")}</option>
            <option value="th">{t("Thai")}</option>
          </select>
        </div>
      </div>

      <div className="settings-field">
        <div className="settings-field-label">{t("Voice Language")}</div>
        <div className="settings-field-control">
          <select 
            className="settings-field-input"
            value={voiceLang}
            onChange={handleVoiceLangChange}
          >
            <option value="auto">{t("Auto-Detect")}</option>
            <option value="thai-cloud">{t("Thai (Cloud)")}</option>
            <option value="english-cloud">{t("English (Cloud)")}</option>
            <option value="thai-offline">{t("Thai (Offline)")}</option>
            <option value="english-offline">{t("English (Offline)")}</option>
          </select>
        </div>
      </div>

      <div className="settings-field">
        <div className="settings-field-label">{t("Voice Speed")}</div>
        <div className="settings-field-control">
          <select 
            className="settings-field-input"
            value={voiceSpeed.toString()}
            onChange={handleVoiceSpeedChange}
          >
            <option value="1">{t("Normal")}</option>
            <option value="1.5">{t("Fast")}</option>
          </select>
        </div>
      </div>

      <div className="settings-field">
        <div className="settings-field-label">{t("Vehicle Marker Style")}</div>
        <div className="settings-field-control">
          <select 
            className="settings-field-input"
            value={vehicleMarkerStyle}
            onChange={(e) => setVehicleMarkerStyle(e.target.value as "default" | "high-vis")}
          >
            <option value="default">{t("Default (Red/Blue)")}</option>
            <option value="high-vis">{t("High Visibility (Green/Gold)")}</option>
          </select>
        </div>
      </div>

      <div className="settings-field">
        <div className="settings-field-label">{t("Map Style")}</div>
        <div className="settings-field-control">
          <select 
            className="settings-field-input"
            value={mapStyle}
            onChange={(e) => setMapStyle(e.target.value as "street" | "satellite")}
          >
            <option value="street">{t("Street (Dark)")}</option>
            <option value="satellite">{t("Satellite (Esri)")}</option>
          </select>
        </div>
      </div>

      {APP_NUMBER_FIELDS.map(({ name, label }) => (
        <NumberSettingField
          key={name}
          group="App"
          name={name}
          label={t(label)}
          error={errors[`App.${name}`]}
          onChange={(value) => setValue("App", name, value)}
        />
      ))}
    </section>
  );
}

type LinkAction = "add" | "remove" | "connect" | "disconnect";

/**
 * Tracks in-flight link requests keyed by request id -> (name, action).
 * Since `linkAck` never carries the resulting {@link LinkConfig} (§16.7),
 * an accepted mutation triggers a fresh `getLinks` (whose `linksValue`
 * response bindBridge.ts already routes into the store) rather than
 * applying anything locally here — except `remove`, which drops the row
 * immediately for snappier feedback ahead of that re-fetch.
 */
function useLinkRequests(client: BridgeClient) {
  const removeLinkLocal = useSettingsStore((state) => state.removeLinkLocal);
  const markPending = useSettingsStore((state) => state.markLinkPending);
  const clearPending = useSettingsStore((state) => state.clearLinkPending);
  const [linkErrors, setLinkErrors] = useState<Record<string, string>>({});
  const [addError, setAddError] = useState<string | null>(null);
  const [addPending, setAddPending] = useState(false);
  const pendingRef = useRef(new Map<string, { name: string; action: LinkAction }>());
  const timersRef = useRef(new Map<string, ReturnType<typeof setTimeout>>());

  const fetchLinks = useCallback(() => {
    client.send({ type: "getLinks", id: nextRequestId("get-links") });
  }, [client]);

  useEffect(() => {
    return client.onLinksResponse((message) => {
      if (message.type !== "linkAck") {
        return;
      }
      const target = pendingRef.current.get(message.id);
      if (!target) {
        return;
      }
      pendingRef.current.delete(message.id);
      const timer = timersRef.current.get(message.id);
      if (timer !== undefined) {
        clearTimeout(timer);
        timersRef.current.delete(message.id);
      }

      if (target.action === "add") {
        setAddPending(false);
        if (message.status === "rejected") {
          setAddError(message.reason ?? "Rejected");
        } else {
          setAddError(null);
          fetchLinks();
        }
        return;
      }

      const { name } = target;
      clearPending(name);
      if (message.status === "rejected") {
        setLinkErrors((prev) => ({ ...prev, [name]: message.reason ?? "Rejected" }));
        return;
      }
      setLinkErrors((prev) => {
        if (!(name in prev)) return prev;
        const { [name]: _dropped, ...rest } = prev;
        return rest;
      });
      if (target.action === "remove") {
        removeLinkLocal(name); // instant; removeLink is synchronous server-side (§16.7/§16.8)
        return;
      }
      // connect/disconnect are fire-and-forget (§16.7) — poll getLinks to observe the transition.
      fetchLinks();
      setTimeout(fetchLinks, LINK_POLL_DELAY_MS);
    });
  }, [client, removeLinkLocal, clearPending, fetchLinks]);

  useEffect(() => {
    return () => {
      for (const timer of timersRef.current.values()) {
        clearTimeout(timer);
      }
    };
  }, []);

  const track = useCallback((id: string, target: { name: string; action: LinkAction }, onTimeout: () => void) => {
    pendingRef.current.set(id, target);
    const timer = setTimeout(() => {
      if (!pendingRef.current.has(id)) return;
      pendingRef.current.delete(id);
      onTimeout();
    }, LINK_TIMEOUT_MS);
    timersRef.current.set(id, timer);
  }, []);

  const sendAdd = useCallback(
    (fields: LinkFieldsInput) => {
      const validated = validateLinkFields(fields);
      if (!validated.ok) {
        setAddError(validated.error);
        return;
      }
      const id = nextRequestId("add-link");
      const sent = client.send({ type: "addLink", id, config: validated.value });
      if (!sent) {
        setAddError("Not connected — unable to send");
        return;
      }
      setAddError(null);
      setAddPending(true);
      track(id, { name: validated.value.name, action: "add" }, () => {
        setAddPending(false);
        setAddError("No response from bridge — try again");
      });
    },
    [client, track],
  );

  const sendConnect = useCallback(
    (name: string) => {
      const id = nextRequestId("connect-link");
      const sent = client.send({ type: "connectLink", id, name });
      if (!sent) {
        setLinkErrors((prev) => ({ ...prev, [name]: "Not connected — unable to send" }));
        return;
      }
      markPending(name, "connect");
      track(id, { name, action: "connect" }, () => {
        clearPending(name);
        setLinkErrors((prev) => ({ ...prev, [name]: "No response from bridge — try again" }));
      });
    },
    [client, markPending, clearPending, track],
  );

  const sendDisconnect = useCallback(
    (name: string) => {
      const id = nextRequestId("disconnect-link");
      const sent = client.send({ type: "disconnectLink", id, name });
      if (!sent) {
        setLinkErrors((prev) => ({ ...prev, [name]: "Not connected — unable to send" }));
        return;
      }
      markPending(name, "disconnect");
      track(id, { name, action: "disconnect" }, () => {
        clearPending(name);
        setLinkErrors((prev) => ({ ...prev, [name]: "No response from bridge — try again" }));
      });
    },
    [client, markPending, clearPending, track],
  );

  const sendRemove = useCallback(
    (name: string) => {
      const id = nextRequestId("remove-link");
      const sent = client.send({ type: "removeLink", id, name });
      if (!sent) {
        setLinkErrors((prev) => ({ ...prev, [name]: "Not connected — unable to send" }));
        return;
      }
      markPending(name, "remove");
      track(id, { name, action: "remove" }, () => {
        clearPending(name);
        setLinkErrors((prev) => ({ ...prev, [name]: "No response from bridge — try again" }));
      });
    },
    [client, markPending, clearPending, track],
  );

  return { linkErrors, addError, addPending, sendAdd, sendConnect, sendDisconnect, sendRemove };
}

function linkSummary(link: LinkConfig): string {
  const config = link.config as Record<string, unknown>;
  if (link.type === "serial") return `${config.port ?? "?"} @ ${config.baud ?? "?"}`;
  if (link.type === "udp") {
    const hosts = Array.isArray(config.targetHosts) ? (config.targetHosts as string[]) : [];
    return hosts.length > 0 ? `:${config.listenPort ?? "?"} -> ${hosts.join(", ")}` : `listen :${config.listenPort ?? "?"}`;
  }
  if (link.type === "tcp") return `${config.host ?? "?"}:${config.port ?? "?"}`;
  return link.type;
}

function LinkRow({
  link,
  error,
  onConnect,
  onDisconnect,
  onRemove,
}: {
  link: LinkConfig;
  error?: string;
  onConnect: () => void;
  onDisconnect: () => void;
  onRemove: () => void;
}) {
  const { t } = useTranslation();
  const pendingOp = useLinkPending(link.name);
  const busy = pendingOp !== undefined;

  return (
    <div className="settings-link-row">
      <div className="settings-link-info">
        <span
          className={`settings-link-status settings-link-status--${link.connected ? "connected" : "disconnected"}`}
          aria-hidden="true"
        />
        <span className="settings-link-name">{link.name}</span>
        <span className="settings-link-type">{link.type}</span>
        <span className="settings-link-detail">{linkSummary(link)}</span>
        {link.autoConnect && <span className="settings-link-auto">{t("auto")}</span>}
      </div>
      <div className="settings-link-actions">
        {link.connected ? (
          <button type="button" onClick={onDisconnect} disabled={busy}>
            {pendingOp === "disconnect" ? t("Disconnecting…") : t("Disconnect")}
          </button>
        ) : (
          <button type="button" onClick={onConnect} disabled={busy}>
            {pendingOp === "connect" ? t("Connecting…") : t("Connect")}
          </button>
        )}
        <button type="button" className="settings-link-remove" onClick={onRemove} disabled={busy}>
          {pendingOp === "remove" ? t("Removing…") : t("Remove")}
        </button>
      </div>
      {error && <span className="settings-field-error">{error}</span>}
    </div>
  );
}

function AddLinkForm({
  pending,
  error,
  onSubmit,
}: {
  pending: boolean;
  error: string | null;
  onSubmit: (fields: LinkFieldsInput) => void;
}) {
  const { t } = useTranslation();
  const [linkType, setLinkType] = useState<AddableLinkType>("udp");
  const [name, setName] = useState("");
  const [path, setPath] = useState("");
  const [baud, setBaud] = useState("57600");
  const [host, setHost] = useState("");
  const [port, setPort] = useState("5760");
  const [listenPort, setListenPort] = useState("14550");

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    onSubmit({ linkType, name, path, baud, host, port, listenPort });
  };

  return (
    <form className="settings-add-link" onSubmit={handleSubmit} aria-label="Add link">
      <div className="settings-add-link-row">
        <select
          aria-label="Link type"
          value={linkType}
          disabled={pending}
          onChange={(e) => setLinkType(e.target.value as AddableLinkType)}
        >
          <option value="udp">UDP</option>
          <option value="tcp">TCP</option>
          <option value="serial">Serial</option>
        </select>
        <input
          type="text"
          placeholder="Name"
          aria-label="Link name"
          value={name}
          disabled={pending}
          onChange={(e) => setName(e.target.value)}
        />
      </div>

      {linkType === "serial" && (
        <div className="settings-add-link-row">
          <input
            type="text"
            placeholder="/dev/ttyUSB0"
            aria-label="Serial path"
            value={path}
            disabled={pending}
            onChange={(e) => setPath(e.target.value)}
          />
          <input
            type="number"
            placeholder="Baud"
            aria-label="Baud rate"
            value={baud}
            disabled={pending}
            onChange={(e) => setBaud(e.target.value)}
          />
        </div>
      )}

      {linkType === "udp" && (
        <div className="settings-add-link-row">
          <input
            type="number"
            placeholder="Listen port"
            aria-label="Listen port"
            value={listenPort}
            disabled={pending}
            onChange={(e) => setListenPort(e.target.value)}
          />
        </div>
      )}

      {linkType === "tcp" && (
        <div className="settings-add-link-row">
          <input
            type="text"
            placeholder="Host"
            aria-label="Host"
            value={host}
            disabled={pending}
            onChange={(e) => setHost(e.target.value)}
          />
          <input
            type="number"
            placeholder="Port"
            aria-label="Port"
            value={port}
            disabled={pending}
            onChange={(e) => setPort(e.target.value)}
          />
        </div>
      )}

      {error && <span className="settings-field-error">{error}</span>}

      <button type="submit" className="settings-add-link-submit" disabled={pending}>
        {pending ? t("Adding…") : t("Add Link")}
      </button>
    </form>
  );
}

function LinksSection({ client }: { client: BridgeClient }) {
  const { t } = useTranslation();
  const links = useLinks();
  const { linkErrors, addError, addPending, sendAdd, sendConnect, sendDisconnect, sendRemove } =
    useLinkRequests(client);

  return (
    <section className="settings-section" aria-label="Links">
      <h3>{t("Links")}</h3>
      <div className="settings-link-list">
        {links.length === 0 ? (
          <div className="settings-empty">{t("No links configured.")}</div>
        ) : (
          links.map((link) => (
            <LinkRow
              key={link.name}
              link={link}
              error={linkErrors[link.name]}
              onConnect={() => sendConnect(link.name)}
              onDisconnect={() => sendDisconnect(link.name)}
              onRemove={() => sendRemove(link.name)}
            />
          ))
        )}
      </div>
      <AddLinkForm pending={addPending} error={addError} onSubmit={sendAdd} />
    </section>
  );
}

export interface SettingsPanelProps {
  client: BridgeClient;
}

export function SettingsPanel({ client }: SettingsPanelProps) {
  return (
    <div className="settings-panel">
      <VideoSection client={client} />
      <AutoConnectSection client={client} />
      <AppSection client={client} />
      <LinksSection client={client} />
    </div>
  );
}
