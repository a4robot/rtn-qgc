# Tauri shell (strangler-fig port, T1a + T1b)

Desktop shell for the RTN QGroundControl port. **The contract is strict:
Tauri is a process manager and a window — nothing else.** All application
logic lives in the **ghost** core (headless QGC) and the web UI. If you
find yourself adding business logic to Rust here, stop: it belongs in
ghost or in the web app.

```
Tauri shell ──spawns/supervises──> ghost (--headless --bridge-port 8877)
     │                                      ▲
     └──window──> web UI (localhost:3000) ──┘  talks to bridge on :8877
```

The shell:

1. opens one window pointed at the web UI (`http://localhost:3000` in dev
   — the Bun dev server; a bundled `frontendDist` in release);
2. spawns the `ghost` sidecar with `--headless --bridge-port 8877`;
3. forwards ghost stdout/stderr into its log;
4. runs a watchdog: on ghost exit it restarts with exponential backoff
   (1/2/4/8/16 s, max 5 tries), emitting a `ghost-status` event to the
   webview: `{ state: "running" | "restarting" | "dead", attempt }`.

## Prerequisites

- Rust (stable) — <https://rustup.rs>
- Tauri CLI v2: `cargo install tauri-cli --version "^2"`
- Platform webview deps — see
  <https://v2.tauri.app/start/prerequisites/> (on Linux:
  `libwebkit2gtk-4.1-dev`, `build-essential`, `libssl-dev`, etc.)
- Bun, for the web UI dev server (lives elsewhere in the repo)

## Wire the dummy sidecar

The real headless QGC binary doesn't exist yet. Use the dummy:

```sh
TRIPLE=$(rustc -vV | sed -n 's/^host: //p')
cp tauri/dummy-ghost/dummy_ghost.sh "tauri/src-tauri/binaries/ghost-$TRIPLE"
chmod +x "tauri/src-tauri/binaries/ghost-$TRIPLE"
```

Details in [`src-tauri/binaries/README.md`](src-tauri/binaries/README.md).

## Run it

```sh
# 1. Start the web UI dev server (from its own directory) on port 3000.
bun run dev

# 2. Start the shell.
cd tauri/src-tauri
cargo tauri dev
```

You should see a window loading `localhost:3000`, and in the shell log a
`ghost: listening on 8877` line from the dummy sidecar. Kill the dummy
process to watch the watchdog restart it with backoff; after 5 failed
restarts the webview receives `ghost-status: { state: "dead" }`.

Listen for status in the web UI:

```js
import { listen } from "@tauri-apps/api/event";
await listen("ghost-status", (e) => console.log(e.payload)); // { state, attempt }
```

## Layout

```
tauri/
├── README.md                     # this file
├── .gitignore                    # target/, gen/schemas/, binaries/ghost-*
├── dummy-ghost/
│   └── dummy_ghost.sh            # dev stand-in for headless QGC
└── src-tauri/
    ├── Cargo.toml                # tauri v2 + tauri-plugin-shell
    ├── build.rs
    ├── tauri.conf.json           # devUrl :3000, externalBin binaries/ghost
    ├── capabilities/default.json # shell sidecar permissions
    ├── binaries/                 # ghost-<triple> goes here (gitignored)
    └── src/
        ├── main.rs
        └── lib.rs                # sidecar spawn + watchdog + ghost-status
```

## Notes

- `bundle.active` is `false` for now: bundling needs app icons, which are
  not part of this scaffold. `cargo tauri dev` does not require them.
- The ghost contract is documented at the top of
  [`src-tauri/src/lib.rs`](src-tauri/src/lib.rs) — keep it in sync with
  the real headless QGC as it lands.
