/**
 * §11.1 tick: 1 Hz unconditional (no subscription), serverTimeUs, vehicleIds.
 */
import { check, TestConn } from "../lib.ts";

export async function runTickGroup(url: string, vehicleId: number): Promise<void> {
  const conn = await TestConn.openAuthed(url);

  // Two ticks, no subscription needed at all -- §11.1: "After helloAck, the server sends tick at
  // 1 Hz unconditionally."
  const tick1 = await conn.next("first tick", (m) => m.type === "tick", 1600);
  check(
    "§11.1 tick carries serverTimeUs (real) + uptimeS + vehicleIds array",
    typeof tick1.serverTimeUs === "number" &&
      Math.abs((tick1.serverTimeUs as number) / 1000 - Date.now()) < 5000 &&
      typeof tick1.uptimeS === "number" &&
      Array.isArray(tick1.vehicleIds),
    tick1,
  );
  check("§11.1 tick.vehicleIds includes the connected MockLink vehicle", (tick1.vehicleIds as number[]).includes(vehicleId), tick1.vehicleIds);

  const t1 = Date.now();
  const tick2 = await conn.next("second tick", (m) => m.type === "tick", 1600);
  const dt = Date.now() - t1;
  check(`§11.1 tick cadence ~1 Hz (observed ${dt}ms between ticks, want 700-1400ms)`, dt >= 700 && dt <= 1400, dt);
  check("§11.1 tick.vehicleIds is stable (state, not an event) across ticks", (tick2.vehicleIds as number[]).includes(vehicleId), tick2.vehicleIds);

  conn.close();
}
