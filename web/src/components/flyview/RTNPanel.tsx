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
  <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
    <path d="M7 18h10v2H7z" />
    <path d="M9 18v-6a3 3 0 0 1 6 0v6" />
    <path d="M12 12v3" />
    <path d="M12 2v3" />
    <path d="M4.5 6.5l2 2" />
    <path d="M19.5 6.5l-2 2" />
  </svg>
);

const SearchlightIcon = () => (
  <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
    <path d="M6 10h4v4H6z" />
    <path d="M10 8h2l2 2v4l-2 2h-2" />
    <path d="M8 14v4h3" />
    <path d="M14 10l8 -3v10l-8 -3" fill="currentColor" fillOpacity="0.2" stroke="none" />
    <path d="M14 10l8 -3" />
    <path d="M14 14l8 3" />
  </svg>
);

const AllAroundIcon = () => (
  <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
    <path d="M12 4C9 8 9 14 9 17a1 1 0 0 0 1 1h4a1 1 0 0 0 1 -1c0 -3 0 -9 -3 -13z" />
    <circle cx="12" cy="11" r="2" />
    <path d="M12 1v1M12 22v1M23 12h-1M2 12h-1M20 4l-1 1M4 20l1-1M20 20l-1-1M4 4l1 1" />
  </svg>
);

const PortIcon = () => (
  <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
    <path d="M12 4C9 8 9 14 9 17a1 1 0 0 0 1 1h4a1 1 0 0 0 1 -1c0 -3 0 -9 -3 -13z" />
    <circle cx="9" cy="10" r="2.5" fill="red" stroke="none" />
  </svg>
);

const StbdIcon = () => (
  <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
    <path d="M12 4C9 8 9 14 9 17a1 1 0 0 0 1 1h4a1 1 0 0 0 1 -1c0 -3 0 -9 -3 -13z" />
    <circle cx="15" cy="10" r="2.5" fill="lime" stroke="none" />
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
