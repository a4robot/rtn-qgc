import { useState } from "react";
import { useParams } from "../../store/index.ts";
import { useBridge } from "../../bridge/BridgeContext.ts";
import { ParamEdit } from "./ParamEdit.tsx";
import "./params.css";

export function ParamTable({ vehicleId = 1 }: { vehicleId?: number }) {
  const params = useParams();
  const bridge = useBridge();
  const [editingPath, setEditingPath] = useState<string | null>(null);
  const [fetchName, setFetchName] = useState("");

  const handleFetch = (e: React.FormEvent) => {
    e.preventDefault();
    if (!fetchName.trim()) return;
    const path = `vehicle.${vehicleId}.${fetchName.trim()}`;
    bridge.send({
      type: "getParam",
      id: `get-${Date.now()}`,
      vehicleId,
      path,
    });
    setFetchName("");
  };

  const paramList = Object.values(params).filter((p) => p.vehicleId === vehicleId);

  return (
    <div className="param-table-container">
      <div className="param-table-header">
        <h2>Parameters</h2>
        <form onSubmit={handleFetch} className="param-fetch-form">
          <input
            type="text"
            value={fetchName}
            onChange={(e) => setFetchName(e.target.value)}
            placeholder="e.g. BAT_N_CELLS"
          />
          <button type="submit" className="btn-fetch">Fetch</button>
        </form>
      </div>

      <div className="table-wrapper">
        <table className="param-table">
          <thead>
            <tr>
              <th>Name</th>
              <th>Value</th>
              <th>Units</th>
              <th>Description</th>
              <th>Action</th>
            </tr>
          </thead>
          <tbody>
            {paramList.length === 0 ? (
              <tr>
                <td colSpan={5} className="param-empty">
                  No parameters loaded.
                </td>
              </tr>
            ) : (
              paramList.map((p) => {
                const name = p.path.split(".").pop();
                return (
                  <tr key={p.path}>
                    <td className="param-name">{name}</td>
                    <td className="param-value">{p.value}</td>
                    <td className="param-units">{p.meta.units || "-"}</td>
                    <td className="param-desc">{p.meta.description || "-"}</td>
                    <td className="param-actions">
                      <button type="button" className="btn-edit" onClick={() => setEditingPath(p.path)}>
                        Edit
                      </button>
                    </td>
                  </tr>
                );
              })
            )}
          </tbody>
        </table>
      </div>

      {editingPath && <ParamEdit path={editingPath} onClose={() => setEditingPath(null)} />}
    </div>
  );
}
