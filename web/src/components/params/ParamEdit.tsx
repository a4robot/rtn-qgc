import { useEffect, useRef, useState } from "react";
import { useParam, useParamStore } from "../../store/index.ts";
import { useBridge } from "../../bridge/BridgeContext.ts";
import { validateParamValue } from "./validateParamValue.ts";
import { useTranslation } from "react-i18next";
import "./params.css";

interface ParamEditProps {
  path: string;
  onClose: () => void;
}

/** How long to wait for a `paramValue` reply before giving up (PROTOCOL.md §6.2). */
const SAVE_TIMEOUT_MS = 5_000;

let requestSeq = 0;
/** Unique per-request id — request/response are keyed by `id`, not the WS message order. */
function nextRequestId(): string {
  requestSeq += 1;
  return `set-${Date.now()}-${requestSeq}`;
}

export function ParamEdit({ path, onClose }: ParamEditProps) {
  const { t } = useTranslation();
  const param = useParam(path);
  const applyParamValue = useParamStore((state) => state.applyParamValue);
  const bridge = useBridge();

  const [text, setText] = useState(() => (param ? String(param.value) : ""));
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const pendingIdRef = useRef<string | null>(null);
  const timeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  // Seed the input once from the value we're opening on; deliberately not
  // re-synced on every store update so an in-flight edit isn't clobbered by
  // an unrelated paramValue push for the same path.
  useEffect(() => {
    if (param) {
      setText(String(param.value));
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [path]);

  useEffect(() => {
    const unsubscribe = bridge.onParamValue((msg) => {
      if (msg.id !== pendingIdRef.current) {
        return; // reply to someone else's request (or a stale one)
      }
      pendingIdRef.current = null;
      if (timeoutRef.current !== null) {
        clearTimeout(timeoutRef.current);
        timeoutRef.current = null;
      }
      setPending(false);
      applyParamValue(msg);
      onClose();
    });
    return () => {
      unsubscribe();
      if (timeoutRef.current !== null) {
        clearTimeout(timeoutRef.current);
      }
    };
  }, [bridge, applyParamValue, onClose]);

  if (!param) return null;

  const meta = param.meta;

  const handleSave = (e: React.FormEvent) => {
    e.preventDefault();
    const result = validateParamValue(text, meta);
    if (!result.ok) {
      setError(result.error);
      return;
    }

    const id = nextRequestId();
    pendingIdRef.current = id;
    const sent = bridge.send({
      type: "setParam",
      id,
      vehicleId: param.vehicleId,
      path: param.path,
      value: result.value,
    });

    if (!sent) {
      pendingIdRef.current = null;
      setError(t("Not connected — unable to send"));
      return;
    }

    setError(null);
    setPending(true);
    timeoutRef.current = setTimeout(() => {
      if (pendingIdRef.current !== id) return; // already resolved
      pendingIdRef.current = null;
      timeoutRef.current = null;
      setPending(false);
      setError(t("No response from vehicle — try again"));
    }, SAVE_TIMEOUT_MS);
  };

  const name = param.path.split(".").pop();

  return (
    <div className="param-modal-overlay">
      <div className="param-modal">
        <header className="param-modal-header">
          <h3>{t("Edit {{name}}", { name })}</h3>
          <button
            type="button"
            className="param-close-btn"
            onClick={onClose}
            disabled={pending}
            aria-label="Close"
          >
            &times;
          </button>
        </header>

        <form onSubmit={handleSave} className="param-modal-body">
          <p className="param-edit-path">{param.path}</p>

          <div className="param-field">
            <label htmlFor="param-edit-value">
              {t("Value")}{meta.units ? ` (${meta.units})` : ""}
            </label>
            <input
              id="param-edit-value"
              type="number"
              step="any"
              value={text}
              onChange={(e) => {
                setText(e.target.value);
                if (error) setError(null);
              }}
              min={meta.min ?? undefined}
              max={meta.max ?? undefined}
              disabled={pending}
              className={error ? "param-input-invalid" : undefined}
              // biome-ignore lint: modal opens on demand, autofocus is expected UX here
              autoFocus
              aria-invalid={error ? true : undefined}
            />
            {error && <p className="param-edit-error">{error}</p>}
          </div>

          <div className="param-meta">
            <p>
              <strong>{t("Current:")}</strong> {param.value}
            </p>
            <p>
              <strong>{t("Type:")}</strong> {meta.type}
            </p>
            <p>
              <strong>{t("Description:")}</strong> {meta.description ?? t("N/A")}
            </p>
            <p>
              <strong>{t("Default:")}</strong> {meta.default ?? t("N/A")}
            </p>
            {meta.min != null && meta.max != null && (
              <p>
                <strong>{t("Range:")}</strong> {meta.min} {t("to")} {meta.max}
              </p>
            )}
          </div>

          <footer className="param-modal-footer">
            <button type="button" className="btn-cancel" onClick={onClose} disabled={pending}>
              {t("Cancel")}
            </button>
            <button type="submit" className="btn-save" disabled={pending}>
              {pending ? t("Saving…") : t("Save")}
            </button>
          </footer>
        </form>
      </div>
    </div>
  );
}
