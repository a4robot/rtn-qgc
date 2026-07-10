/**
 * Pure validation for the Add Link form — shared by {@link SettingsPanel}
 * and its tests, same rationale as params/validateParamValue.ts: the raw
 * (string) form fields in, either a wire-ready `addLink.config` payload or
 * an error message out, with no bridge/React involved so the rule is
 * unit-testable directly.
 *
 * Field set per PROTOCOL.md §16.7's addLink examples/table: serial needs
 * `port` (device path) + `baud`, udp needs `listenPort`, tcp needs
 * `host` + `port`. `config.name` is required for every type. Note there is
 * no `autoConnect` field on `addLink` (SettingsChannel::_handleAddLink()
 * doesn't read one) — a new link is never auto-connecting; toggle
 * `AutoConnect` settings (§16.2) or `connectLink` (§16.7) separately.
 */

import type { AddLink, AddableLinkType } from "../../bridge/types.ts";

export interface LinkFieldsInput {
  linkType: AddableLinkType;
  name: string;
  /** Serial only: device path, e.g. `/dev/ttyUSB0`. */
  path?: string;
  /** Serial only, raw text from a number input. */
  baud?: string;
  /** udp only, raw text from a number input. */
  listenPort?: string;
  /** tcp only. */
  host?: string;
  /** tcp only, raw text from a number input. */
  port?: string;
}

export type LinkFieldsValidation =
  | { ok: true; value: AddLink["config"] }
  | { ok: false; error: string };

function validatePortNumber(raw: string | undefined, label: string): { ok: true; value: number } | { ok: false; error: string } {
  const trimmed = (raw ?? "").trim();
  if (trimmed === "") {
    return { ok: false, error: `${label} is required` };
  }
  const port = Number(trimmed);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    return { ok: false, error: `${label} must be an integer between 1 and 65535` };
  }
  return { ok: true, value: port };
}

/** Validate the Add Link form fields, dispatching on `linkType` for the per-type required fields (PROTOCOL.md §16.7). */
export function validateLinkFields(input: LinkFieldsInput): LinkFieldsValidation {
  const name = input.name.trim();
  if (name === "") {
    return { ok: false, error: "Name is required" };
  }

  if (input.linkType === "serial") {
    const path = (input.path ?? "").trim();
    if (path === "") {
      return { ok: false, error: "Serial path is required (e.g. /dev/ttyUSB0)" };
    }
    const baudTrimmed = (input.baud ?? "").trim();
    const baud = Number(baudTrimmed);
    if (baudTrimmed === "" || !Number.isInteger(baud) || baud <= 0) {
      return { ok: false, error: "Baud rate must be a positive integer" };
    }
    return { ok: true, value: { type: "serial", name, port: path, baud } };
  }

  if (input.linkType === "udp") {
    const listenPort = validatePortNumber(input.listenPort, "Listen port");
    if (!listenPort.ok) {
      return listenPort;
    }
    return { ok: true, value: { type: "udp", name, listenPort: listenPort.value } };
  }

  // tcp
  const host = (input.host ?? "").trim();
  if (host === "") {
    return { ok: false, error: "Host is required" };
  }
  const port = validatePortNumber(input.port, "Port");
  if (!port.ok) {
    return port;
  }
  return { ok: true, value: { type: "tcp", name, host, port: port.value } };
}
