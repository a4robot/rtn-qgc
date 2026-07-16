import crowdin from "../src/locales/th/crowdin.json" assert { type: "json" };

const strings = [
  "No vehicle data",
  "Not connected — command not sent",
  "Takeoff alt (m)",
  "DISARM",
  "ARM",
  "TAKEOFF",
  "LAND",
  "RTL",
  "PAUSE"
];

const missing = [];
for (const str of strings) {
  if (!crowdin[str]) missing.push(str);
}
console.log(JSON.stringify(missing, null, 2));
