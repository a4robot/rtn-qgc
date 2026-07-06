# RTN Ghost GCS — Web Frontend

Web UI for the strangler-fig port of QGroundControl (milestone W0a scaffold).

## Run

```sh
cd web
bun install && bun run dev
```

Then open http://localhost:3000 (override the port with `PORT=...`).

Bun-only stack: `Bun.serve()` with HTML imports handles bundling, TypeScript,
JSX, and CSS — no Vite, webpack, or express. HMR is enabled via
`development: { hmr: true }` in `serve.ts`.

## Layout

```
web/
├── index.html            # entry, imports src/main.tsx
├── serve.ts              # Bun.serve() dev server (bun --hot ./serve.ts)
├── src/
│   ├── main.tsx          # React root
│   ├── App.tsx           # GCS shell: Video / Map / Telemetry / Actions grid
│   ├── app.css           # plain CSS (dark theme), no Tailwind
│   └── bridge/           # framework-agnostic bridge layer (no React here)
│       ├── BridgeClient.ts  # WebSocket client: reconnect w/ backoff,
│       │                    # subscribe(channel, cb), send(), seq-gap stub
│       └── types.ts         # protocol types: Telemetry, Command, Tick
```

## Architecture

**Dumb UI.** The web app renders state pushed by the bridge and sends user
intents (commands) back. All business logic — vehicle state machines, MAVLink,
mission logic, safety checks — lives on the bridge/backend side, not here.

**State, not events.** The bridge publishes full state snapshots per channel.
The UI always renders the latest snapshot and never reconstructs state from an
event history. Consequences baked into the protocol types (`src/bridge/types.ts`):

- every channel message carries a monotonic per-channel `seq` — a gap means
  "request a fresh snapshot", never "replay missed events";
- every vehicle-scoped message carries a `vehicleId` (multi-vehicle from day one);
- commands are not queued while disconnected — a stale command is worse than a
  dropped one.

`BridgeClient` is deliberately framework-agnostic; React integration happens in
UI-side glue (hooks), never inside `src/bridge/`.

## Test / Verify

```sh
bun test                      # unit tests
bun build ./index.html --outdir /tmp/webcheck   # bundle check
```
