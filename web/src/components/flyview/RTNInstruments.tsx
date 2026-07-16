import { useTranslation } from "react-i18next";
import "./rtn-instruments.css";

export function RTNInstruments() {
  const { t } = useTranslation();

  // Hardcoded placeholder values for now
  const rudderAngle = 0;
  const fuelPercent = 0;
  const volts = 0.0;
  const rpm = 0;

  // Rudder rotation mapping (assuming -45 to 45 deg)
  const needleRotation = rudderAngle;

  return (
    <div className="rtn-inst-panel">
      <div className="rtn-inst-top">
        <div className="rtn-inst-title">{t("RUDDER")} {rudderAngle}&deg;</div>
        
        <div className="rtn-rudder-gauge">
          {/* Semicircle outline */}
          <svg viewBox="-50 -50 100 50" className="rtn-rudder-svg">
            <path d="M -40 0 A 40 40 0 0 1 40 0" fill="none" stroke="white" strokeWidth="2" />
            <line x1="0" y1="0" x2="0" y2="-40" stroke="white" strokeWidth="2" />
            <line x1="-28.28" y1="-28.28" x2="-35" y2="-35" stroke="white" strokeWidth="2" />
            <line x1="28.28" y1="-28.28" x2="35" y2="-35" stroke="white" strokeWidth="2" />
            <line x1="-40" y1="0" x2="-45" y2="0" stroke="white" strokeWidth="2" />
            <line x1="40" y1="0" x2="45" y2="0" stroke="white" strokeWidth="2" />
          </svg>
          
          {/* Needle */}
          <div 
            className="rtn-rudder-needle-container" 
            style={{ transform: `rotate(${needleRotation}deg)` }}
          >
            <div className="rtn-rudder-needle"></div>
            <div className="rtn-rudder-dot"></div>
          </div>
        </div>
      </div>

      <div className="rtn-inst-bottom">
        <div className="rtn-bar-col">
          <div className="rtn-bar-title">{t("FUEL")}</div>
          <div className="rtn-bar-container">
            <span className="rtn-bar-label top">F</span>
            <div className="rtn-bar-outline">
              <div className="rtn-bar-fill" style={{ height: `${fuelPercent}%` }}></div>
            </div>
            <span className="rtn-bar-label bottom">E</span>
          </div>
          <div className="rtn-bar-value">{fuelPercent}%</div>
        </div>

        <div className="rtn-bar-col">
          <div className="rtn-bar-title">{t("VOLTS")}</div>
          <div className="rtn-bar-container">
            <div className="rtn-bar-outline">
              <div className="rtn-bar-fill" style={{ height: `${(volts/30)*100}%` }}></div>
            </div>
          </div>
          <div className="rtn-bar-value">{volts.toFixed(1)}V</div>
        </div>

        <div className="rtn-bar-col">
          <div className="rtn-bar-title">{t("RPM")}</div>
          <div className="rtn-bar-container">
            <div className="rtn-bar-outline">
              <div className="rtn-bar-fill" style={{ height: `${(rpm/5000)*100}%` }}></div>
            </div>
          </div>
          <div className="rtn-bar-value">{rpm}</div>
        </div>
      </div>
    </div>
  );
}
