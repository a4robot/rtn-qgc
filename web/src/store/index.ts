/** Store barrel — components import from here, not the individual modules. */

export {
  useConnection,
  useConnectionStore,
  type ConnectionStoreState,
  type TickMessage,
} from "./connectionStore.ts";
export {
  useVehicle,
  useVehicles,
  useVehicleStore,
  type VehicleState,
  type VehicleStoreState,
} from "./vehicleStore.ts";
export {
  useParam,
  useParams,
  useParamStore,
  type ParamStoreState,
} from "./paramStore.ts";
export {
  useMission,
  useMissions,
  useMissionStore,
  type MissionRecord,
  type MissionStoreState,
} from "./missionStore.ts";
export { bindBridgeToStores } from "./bindBridge.ts";
export {
  useActiveVehicle,
  useUiStore,
  useVehicleSwitcher,
  type UiStoreState,
  type VehicleSwitchTarget,
} from "./uiStore.ts";
