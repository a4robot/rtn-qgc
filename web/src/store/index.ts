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
export { bindBridgeToStores } from "./bindBridge.ts";
