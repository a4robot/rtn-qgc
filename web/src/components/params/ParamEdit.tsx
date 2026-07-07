import { useState, useEffect } from "react";
import { useParam } from "../../store/index.ts";
import { useBridge } from "../../bridge/BridgeContext.ts";
import "./params.css";

interface ParamEditProps {
  path: string;
  onClose: () => void;
}

export function ParamEdit({ path, onClose }: ParamEditProps) {
  const param = useParam(path);
  const bridge = useBridge();
  const [value, setValue] = useState("");

  useEffect(() => {
    if (param) {
      setValue(String(param.value));
    }
  }, [param]);

  if (!param) return null;

  const handleSave = (e: React.FormEvent) => {
    e.preventDefault();
    const numValue = parseFloat(value);
    if (isNaN(numValue)) return;
    
    bridge.send({
      type: "setParam",
      id: `set-${Date.now()}`,
      vehicleId: param.vehicleId,
      path: param.path,
      value: numValue,
    });
    
    onClose();
  };

  const name = param.path.split(".").pop();

  return (
    <div className="param-modal-overlay">
      <div className="param-modal">
        <header className="param-modal-header">
          <h3>Edit {name}</h3>
          <button type="button" className="param-close-btn" onClick={onClose}>&times;</button>
        </header>
        
        <form onSubmit={handleSave} className="param-modal-body">
          <div className="param-field">
            <label>Value ({param.meta.units || "no units"})</label>
            <input 
              type="number" 
              step="any"
              value={value} 
              onChange={e => setValue(e.target.value)}
              min={param.meta.min ?? undefined}
              max={param.meta.max ?? undefined}
              required
            />
          </div>
          
          <div className="param-meta">
            <p><strong>Description:</strong> {param.meta.description || "N/A"}</p>
            <p><strong>Type:</strong> {param.meta.type}</p>
            <p><strong>Default:</strong> {param.meta.default !== null ? param.meta.default : "N/A"}</p>
            {param.meta.min !== null && param.meta.max !== null && (
              <p><strong>Range:</strong> {param.meta.min} to {param.meta.max}</p>
            )}
          </div>
          
          <footer className="param-modal-footer">
            <button type="button" className="btn-cancel" onClick={onClose}>Cancel</button>
            <button type="submit" className="btn-save">Save</button>
          </footer>
        </form>
      </div>
    </div>
  );
}
