import { expect, test } from "bun:test";

import { mapModeForTab, tabForMapMode } from "./sidePanelSync.ts";

test("selecting the FLY tab drives mapMode to fly", () => {
  expect(mapModeForTab("fly", "plan")).toBe("fly");
});

test("selecting the PLAN tab drives mapMode to plan", () => {
  expect(mapModeForTab("plan", "fly")).toBe("plan");
});

test("selecting the PARAMS tab leaves mapMode untouched", () => {
  expect(mapModeForTab("params", "fly")).toBe("fly");
  expect(mapModeForTab("params", "plan")).toBe("plan");
});

test("an external mapMode change to fly syncs the tab to fly", () => {
  expect(tabForMapMode("fly")).toBe("fly");
});

test("an external mapMode change to plan syncs the tab to plan", () => {
  expect(tabForMapMode("plan")).toBe("plan");
});
