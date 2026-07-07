import { create } from "zustand";
import type { ParamValue } from "../bridge/types.ts";

export interface ParamStoreState {
  params: Record<string, ParamValue>;
  applyParamValue: (paramValue: ParamValue) => void;
  clear: () => void;
}

export const useParamStore = create<ParamStoreState>()((set) => ({
  params: {
    "vehicle.1.MPC_XY_VEL_MAX": {
      type: "paramValue",
      id: "mock-1",
      vehicleId: 1,
      path: "vehicle.1.MPC_XY_VEL_MAX",
      value: 12.0,
      meta: {
        type: "float",
        units: "m/s",
        min: 0.0,
        max: 20.0,
        default: 12.0,
        description: "Maximum horizontal velocity"
      }
    },
    "vehicle.1.BAT_N_CELLS": {
      type: "paramValue",
      id: "mock-2",
      vehicleId: 1,
      path: "vehicle.1.BAT_N_CELLS",
      value: 6,
      meta: {
        type: "int32",
        units: null,
        min: 1,
        max: 16,
        default: 6,
        description: "Number of cells in the battery"
      }
    }
  },
  applyParamValue: (paramValue) =>
    set((prev) => ({
      params: {
        ...prev.params,
        [paramValue.path]: paramValue,
      },
    })),
  clear: () => set({ params: {} }),
}));

export function useParams(): Record<string, ParamValue> {
  return useParamStore((state) => state.params);
}

export function useParam(path: string): ParamValue | undefined {
  return useParamStore((state) => state.params[path]);
}
