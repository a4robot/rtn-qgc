import { useTranslation } from "react-i18next";
import "./rtn-panel.css";

const CameraIcon = () => (
  <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
    <path d="M14.5 4h-5L7 7H4a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V9a2 2 0 0 0-2-2h-3l-2.5-3z" />
    <circle cx="12" cy="13" r="3" />
  </svg>
);

const UpIcon = () => (
  <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
    <polyline points="18 15 12 9 6 15" />
  </svg>
);

const DownIcon = () => (
  <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
    <polyline points="6 9 12 15 18 9" />
  </svg>
);

const SirenIcon = () => (
  <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" style={{filter: 'drop-shadow(0 0 2px rgba(255,255,255,0.5))'}}>
    <path d="M9 19h6a1 1 0 0 0 1-1v-1a1 1 0 0 0-1-1H9a1 1 0 0 0-1 1v1a1 1 0 0 0 1 1z" />
    <path d="M10 16V9c0-1.5 1-3 2-3s2 1.5 2 3v7" />
    <path d="M12 9v5" strokeWidth="2" stroke="var(--status-critical)" />
    <path d="M8 12a4 4 0 0 1 8 0" strokeDasharray="2 2" />
    <path d="M12 2v2M6.5 5.5l1 1M17.5 5.5l-1 1" strokeWidth="1.5" />
  </svg>
);

const SearchlightIcon = () => (
  <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" style={{filter: 'drop-shadow(0 0 2px rgba(255,255,255,0.5))'}}>
    <path d="M7 10h5a2 2 0 0 1 2 2v0a2 2 0 0 1-2 2H7a1 1 0 0 1-1-1v-2a1 1 0 0 1 1-1z" />
    <path d="M14 10l6-3v10l-6-3" fill="currentColor" fillOpacity="0.2" stroke="none" />
    <path d="M14 10l6-3M14 14l6 3" />
    <path d="M9 14v3a1 1 0 0 0 2 0v-3" />
    <ellipse cx="13" cy="12" rx="1" ry="2" />
  </svg>
);

const AllAroundIcon = () => (
  <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
    <path d="M12 7c-2 2-2 5-2 8a1 1 0 0 0 1 1h2a1 1 0 0 0 1-1c0-3 0-6-2-8z" />
    <path d="M11.5 13h1" />
    {/* Surround lights */}
    <g strokeWidth="1" strokeDasharray="1 3">
      <circle cx="12" cy="12" r="7" />
    </g>
    <path d="M12 2v1M12 21v1M2 12h1M21 12h1M4.5 4.5l1 1M18.5 18.5l1 1M19.5 4.5l-1 1M4.5 19.5l1-1" strokeWidth="1" />
  </svg>
);

const PortIcon = () => (
  <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
    <path d="M12 6c-2 3-2 7-2 10a1 1 0 0 0 1 1h2a1 1 0 0 0 1-1c0-3 0-7-2-10z" />
    <path d="M11.5 13h1" />
    <circle cx="9.5" cy="11" r="1.5" fill="var(--status-critical)" stroke="none" style={{filter: 'drop-shadow(0 0 3px var(--status-critical))'}} />
  </svg>
);

const StbdIcon = () => (
  <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
    <path d="M12 6c-2 3-2 7-2 10a1 1 0 0 0 1 1h2a1 1 0 0 0 1-1c0-3 0-7-2-10z" />
    <path d="M11.5 13h1" />
    <circle cx="14.5" cy="11" r="1.5" fill="var(--status-ok)" stroke="none" style={{filter: 'drop-shadow(0 0 3px var(--status-ok))'}} />
  </svg>
);

export function RTNPanel() {
  const { t } = useTranslation();

  return (
    <div className="rtn-panel">
      <div className="rtn-panel-group">
        <button type="button" className="rtn-panel-btn">
          <CameraIcon />
          <span>{t("FPV")}</span>
        </button>
        <button type="button" className="rtn-panel-btn">
          <UpIcon />
          <span>{t("Trim Up")}</span>
        </button>
        <button type="button" className="rtn-panel-btn">
          <DownIcon />
          <span>{t("Trim Dn")}</span>
        </button>
      </div>

      <div className="rtn-panel-group">
        <button type="button" className="rtn-panel-btn">
          <SirenIcon />
          <span>{t("Siren")}</span>
        </button>
        <button type="button" className="rtn-panel-btn">
          <AllAroundIcon />
          <span>{t("All Ard.")}</span>
        </button>
        <button type="button" className="rtn-panel-btn">
          <SearchlightIcon />
          <span>{t("Nav.")}</span>
        </button>
        <button type="button" className="rtn-panel-btn">
          <PortIcon />
          <span>{t("Port")}</span>
        </button>
        <button type="button" className="rtn-panel-btn">
          <StbdIcon />
          <span>{t("Stbd.")}</span>
        </button>
      </div>
    </div>
  );
}
