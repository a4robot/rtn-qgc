/**
 * §16 settings channel: app settings get/set round-trip against the server-enforced whitelist,
 * whitelist rejection, settingChanged broadcast to a second client, and link management CRUD
 * lifecycle (add/get/connect/disconnect/remove) against a throwaway UDP link.
 *
 * Persistence (§16.8) is NOT exercised here -- this suite runs against one continuously-running
 * ghost process per invocation and has no fixture for restarting it mid-run (see run.ts's header
 * comment for the same limitation applying to every group). §16.8 documents the manual check.
 */
import { check, TestConn } from "../lib.ts";

export async function runSettingsGroup(url: string): Promise<void> {
  const conn = await TestConn.openAuthed(url);

  // --- §16.3: getSettings with no group -> every whitelisted setting from every group.
  conn.send({ type: "getSettings", id: "gs-all" });
  const allSettings = await conn.next("getSettings (all)", (m) => m.type === "settingsValue" && m.id === "gs-all");
  const settingsArray = (allSettings.settings ?? []) as Record<string, unknown>[];
  check("§16.3 getSettings (no group) returns a non-empty settings array", Array.isArray(settingsArray) && settingsArray.length > 0, settingsArray.length);
  const groupsSeen = new Set(settingsArray.map((s) => s.group));
  check("§16.2 whitelist spans Video/App/AutoConnect", groupsSeen.has("Video") && groupsSeen.has("App") && groupsSeen.has("AutoConnect"), [...groupsSeen]);
  const videoSourceEntry = settingsArray.find((s) => s.group === "Video" && s.name === "videoSource");
  check(
    "§16.3 videoSource entry carries meta.type=string and a non-empty meta.enumValues",
    typeof videoSourceEntry?.value === "string" &&
      (videoSourceEntry?.meta as Record<string, unknown>)?.type === "string" &&
      Array.isArray((videoSourceEntry?.meta as Record<string, unknown>)?.enumValues) &&
      ((videoSourceEntry?.meta as Record<string, unknown>).enumValues as unknown[]).length > 0,
    videoSourceEntry,
  );

  // --- §16.3: getSettings with a known group -> only that group's entries.
  conn.send({ type: "getSettings", id: "gs-video", group: "Video" });
  const videoSettings = await conn.next("getSettings (Video)", (m) => m.type === "settingsValue" && m.id === "gs-video");
  const videoArray = (videoSettings.settings ?? []) as Record<string, unknown>[];
  check("§16.3 getSettings(group=Video) returns only Video entries", videoArray.length > 0 && videoArray.every((s) => s.group === "Video"), videoArray);
  check(
    "§16.2 Video whitelist includes videoSource/udpUrl/rtspUrl/tcpUrl/streamEnabled/lowLatencyMode",
    ["videoSource", "udpUrl", "rtspUrl", "tcpUrl", "streamEnabled", "lowLatencyMode"].every((n) => videoArray.some((s) => s.name === n)),
    videoArray.map((s) => s.name),
  );

  // --- §16.3/§10: getSettings with an unknown group -> UNKNOWN_SETTINGS_GROUP.
  conn.send({ type: "getSettings", id: "gs-bogus", group: "Nonexistent" });
  const bogusGroup = await conn.next("getSettings (unknown group) error", (m) => m.type === "error" && m.id === "gs-bogus");
  check("§16.3 getSettings(unknown group) -> error UNKNOWN_SETTINGS_GROUP", bogusGroup.code === "UNKNOWN_SETTINGS_GROUP", bogusGroup);

  // --- §16.4: setSetting on a whitelisted App entry -> readback settingsValue with the new value.
  // Save the prior value first so this test can restore it (defaultMissionItemAltitude is a real
  // App setting consumed by MissionController et al, §16.2's table).
  const priorAltEntry = settingsArray.find((s) => s.group === "App" && s.name === "defaultMissionItemAltitude");
  const priorAlt = typeof priorAltEntry?.value === "number" ? (priorAltEntry.value as number) : 50;
  const testAlt = priorAlt === 77 ? 88 : 77;
  conn.send({ type: "setSetting", id: "ss-alt", group: "App", name: "defaultMissionItemAltitude", value: testAlt });
  const altAck = await conn.next("setSetting defaultMissionItemAltitude ack", (m) => m.type === "settingsValue" && m.id === "ss-alt");
  const altAckEntry = ((altAck.settings ?? []) as Record<string, unknown>[])[0];
  check(
    "§16.4 setSetting readback echoes the value just set",
    altAckEntry?.group === "App" && altAckEntry?.name === "defaultMissionItemAltitude" && altAckEntry?.value === testAlt,
    altAckEntry,
  );

  // --- §16.4/§10: setSetting on a name outside the whitelist -> SETTING_NOT_ALLOWED. aspectRatio
  // is a real VideoSettings Fact (App.SettingsGroup.json-adjacent) but NOT in the §16.2 table.
  conn.send({ type: "setSetting", id: "ss-notallowed", group: "Video", name: "aspectRatio", value: 1.5 });
  const notAllowed = await conn.next("setSetting (not whitelisted) error", (m) => m.type === "error" && m.id === "ss-notallowed");
  check("§16.4 setSetting outside whitelist -> error SETTING_NOT_ALLOWED", notAllowed.code === "SETTING_NOT_ALLOWED", notAllowed);

  // --- §16.4/§10: setSetting with a group that doesn't exist at all -> same SETTING_NOT_ALLOWED.
  conn.send({ type: "setSetting", id: "ss-badgroup", group: "Nonexistent", name: "whatever", value: 1 });
  const badGroupSet = await conn.next("setSetting (unknown group) error", (m) => m.type === "error" && m.id === "ss-badgroup");
  check("§16.4 setSetting on unknown group -> error SETTING_NOT_ALLOWED", badGroupSet.code === "SETTING_NOT_ALLOWED", badGroupSet);

  // --- §16.4/§10: setSetting with a value of the wrong JSON type -> VALUE_OUT_OF_RANGE.
  conn.send({ type: "setSetting", id: "ss-badtype", group: "Video", name: "streamEnabled", value: "yes" });
  const badType = await conn.next("setSetting (wrong JSON type) error", (m) => m.type === "error" && m.id === "ss-badtype");
  check("§16.4 setSetting with wrong JSON type -> error VALUE_OUT_OF_RANGE", badType.code === "VALUE_OUT_OF_RANGE", badType);

  // --- §16.4/§10: setSetting videoSource with a value outside meta.enumValues -> VALUE_OUT_OF_RANGE.
  conn.send({ type: "setSetting", id: "ss-badenum", group: "Video", name: "videoSource", value: "Not A Real Source" });
  const badEnum = await conn.next("setSetting (not in enumValues) error", (m) => m.type === "error" && m.id === "ss-badenum");
  check("§16.4 setSetting videoSource outside enumValues -> error VALUE_OUT_OF_RANGE", badEnum.code === "VALUE_OUT_OF_RANGE", badEnum);

  // --- §16.5: settingChanged broadcasts to every connected client, including a SECOND client that
  // did not itself send the setSetting.
  const conn2 = await TestConn.openAuthed(url);
  conn.send({ type: "setSetting", id: "ss-broadcast", group: "App", name: "defaultMissionItemAltitude", value: priorAlt });
  const ownAck = await conn.next("setSetting (restore) ack", (m) => m.type === "settingsValue" && m.id === "ss-broadcast");
  check("§16.4 setSetting (restore) readback ok", ((ownAck.settings ?? []) as Record<string, unknown>[])[0]?.value === priorAlt, ownAck);
  const isRestoreChange = (m: Record<string, unknown>) => m.type === "settingChanged" && m.group === "App" && m.name === "defaultMissionItemAltitude" && m.value === priorAlt;
  const selfBroadcast = await conn.next("settingChanged on the requester's own connection", isRestoreChange, 2000);
  check("§16.5 settingChanged reaches the requester itself", selfBroadcast.value === priorAlt, selfBroadcast);
  const otherBroadcast = await conn2.next("settingChanged on a second, uninvolved connection", isRestoreChange, 2000);
  check(
    "§16.5 settingChanged reaches a second client that never subscribed to anything (no subscribe exists for settings)",
    otherBroadcast.group === "App" && otherBroadcast.name === "defaultMissionItemAltitude" && otherBroadcast.value === priorAlt && typeof otherBroadcast.timeUs === "number",
    otherBroadcast,
  );
  conn2.close();

  // --- §16.7 link management lifecycle: getLinks baseline -> addLink (throwaway UDP port) ->
  // getLinks shows it -> connectLink -> disconnectLink -> removeLink -> getLinks no longer shows it.
  const testLinkName = `conformance-udp-${Date.now()}`;
  const testLinkPort = 25680 + (Date.now() % 200); // spread across reruns without a fixed collision

  conn.send({ type: "getLinks", id: "gl-before" });
  const linksBefore = await conn.next("getLinks (before addLink)", (m) => m.type === "linksValue" && m.id === "gl-before");
  check("§16.7 getLinks (before) does not already contain the test link", !((linksBefore.links as Record<string, unknown>[]) ?? []).some((l) => l.name === testLinkName), linksBefore);

  conn.send({ type: "addLink", id: "al-1", config: { type: "udp", name: testLinkName, listenPort: testLinkPort } });
  const addAck = await conn.next("addLink ack", (m) => m.type === "linkAck" && m.id === "al-1");
  check("§16.7 addLink (udp, throwaway port) -> linkAck accepted", addAck.status === "accepted" && addAck.name === testLinkName, addAck);

  conn.send({ type: "addLink", id: "al-dup", config: { type: "udp", name: testLinkName, listenPort: testLinkPort + 1 } });
  const dupAck = await conn.next("addLink (duplicate name) ack", (m) => m.type === "linkAck" && m.id === "al-dup");
  check("§16.7 addLink with a duplicate name -> linkAck rejected", dupAck.status === "rejected" && typeof dupAck.reason === "string", dupAck);

  conn.send({ type: "getLinks", id: "gl-after-add" });
  const linksAfterAdd = await conn.next("getLinks (after addLink)", (m) => m.type === "linksValue" && m.id === "gl-after-add");
  const addedEntry = ((linksAfterAdd.links as Record<string, unknown>[]) ?? []).find((l) => l.name === testLinkName);
  check(
    "§16.7 getLinks reflects the new udp link with the requested listenPort, not yet connected",
    addedEntry?.type === "udp" && (addedEntry?.config as Record<string, unknown>)?.listenPort === testLinkPort && addedEntry?.connected === false,
    addedEntry,
  );

  conn.send({ type: "connectLink", id: "cl-1", name: testLinkName });
  const connectAck = await conn.next("connectLink ack", (m) => m.type === "linkAck" && m.id === "cl-1");
  check("§16.7 connectLink -> linkAck accepted (fire-and-forget, §16.7)", connectAck.status === "accepted" && connectAck.name === testLinkName, connectAck);

  // UDP's own connect is dispatched onto a worker thread but is near-instant (no handshake) --
  // poll getLinks briefly rather than asserting connected on the very next message.
  let observedConnected = false;
  for (let i = 0; i < 20 && !observedConnected; i++) {
    conn.send({ type: "getLinks", id: `gl-poll-${i}` });
    const poll = await conn.next(`getLinks (poll ${i})`, (m) => m.type === "linksValue" && m.id === `gl-poll-${i}`, 2000);
    const entry = ((poll.links as Record<string, unknown>[]) ?? []).find((l) => l.name === testLinkName);
    observedConnected = entry?.connected === true;
    if (!observedConnected) {
      await Bun.sleep(100);
    }
  }
  check("§16.7 connectLink eventually shows connected:true via getLinks polling", observedConnected);

  conn.send({ type: "disconnectLink", id: "dl-1", name: testLinkName });
  const disconnectAck = await conn.next("disconnectLink ack", (m) => m.type === "linkAck" && m.id === "dl-1");
  check("§16.7 disconnectLink -> linkAck accepted", disconnectAck.status === "accepted" && disconnectAck.name === testLinkName, disconnectAck);

  conn.send({ type: "removeLink", id: "rl-1", name: testLinkName });
  const removeAck = await conn.next("removeLink ack", (m) => m.type === "linkAck" && m.id === "rl-1");
  check("§16.7 removeLink -> linkAck accepted", removeAck.status === "accepted" && removeAck.name === testLinkName, removeAck);

  const bogusName = "no-such-link-conformance";
  conn.send({ type: "removeLink", id: "rl-bogus", name: bogusName });
  const removeBogus = await conn.next("removeLink (unknown name) ack", (m) => m.type === "linkAck" && m.id === "rl-bogus");
  check("§16.7 removeLink on an unknown name -> linkAck rejected (not a §10 error)", removeBogus.status === "rejected" && typeof removeBogus.reason === "string", removeBogus);

  conn.send({ type: "getLinks", id: "gl-after-remove" });
  const linksAfterRemove = await conn.next("getLinks (after removeLink)", (m) => m.type === "linksValue" && m.id === "gl-after-remove");
  check(
    "§16.7 getLinks no longer contains the removed test link",
    !((linksAfterRemove.links as Record<string, unknown>[]) ?? []).some((l) => l.name === testLinkName),
    linksAfterRemove,
  );

  conn.close();
}
