import { createContext, useContext } from "react";
import type { BridgeClient } from "./BridgeClient.ts";

export const BridgeContext = createContext<BridgeClient | null>(null);

export function useBridge(): BridgeClient {
  const client = useContext(BridgeContext);
  if (!client) {
    throw new Error("useBridge must be used within a BridgeContext.Provider");
  }
  return client;
}
