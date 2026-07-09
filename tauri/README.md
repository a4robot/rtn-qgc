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
2. spawns the `ghost` sidecar with `--headless --bridge-port 8877` (plus
   `--mock-link` iff `RTN_GHOST_MOCK=1` is set in the shell's own
   environment — dev/demo only, see below), with `LD_LIBRARY_PATH` /
   `QT_PLUGIN_PATH` pointed at ghost's bundled Qt6 closure and
   `QT_QPA_PLATFORM=offscreen` forced;
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

## Wire the ghost sidecar

As of Wave 13 the real headless QGC ghost is wired in — no more
Docker-wrapper stopgap. Stage it from the host-portable bundle (see
[`src-tauri/binaries/README.md`](src-tauri/binaries/README.md) for how
that bundle is built):

```sh
tauri/scripts/stage-ghost-bundle.sh [path-to-ghost-bundle] [target-triple]
# defaults: /home/bpasu/ghost-bundle, host triple
```

This copies `bundle/bin/QGroundControl` to
`src-tauri/binaries/ghost-<target-triple>` (the `bundle.externalBin`
sidecar) and `bundle/{lib,plugins}` to
`src-tauri/binaries/ghost-resources/{lib,plugins}` (`bundle.resources` —
Tauri lays these out next to the installed app and `../src/lib.rs`
resolves them at runtime via `app.path().resource_dir()` to set
`LD_LIBRARY_PATH`/`QT_PLUGIN_PATH` on the sidecar; see the module doc at
the top of that file for the full contract). All of this is gitignored —
never committed, always staged locally or by CI before building.

For a plain dev stand-in that doesn't need the real bundle (dummy sidecar
that just sleeps and logs a readiness line), use:

```sh
tauri/scripts/link-ghost.sh tauri/dummy-ghost/dummy_ghost.sh
```

**Some `binaries/ghost-<target-triple>` file is required even for
`cargo check` / `cargo build`**, not just `cargo tauri dev`:
`tauri-build`'s codegen validates that every `bundle.externalBin`
resource resolves to a file on disk at compile time, regardless of
`bundle.active`. Without it, the build fails with `resource path
"binaries/ghost-<target-triple>" doesn't exist` before your code is even
checked.

### Dev/demo mock-link toggle

Production spawns ghost with no `--mock-link` flag (real vehicle
connections only). To get the MockLink vehicle (128) for local dev or
demos, set `RTN_GHOST_MOCK=1` in the shell's environment before launching
it (`cargo tauri dev`, or the installed app: `RTN_GHOST_MOCK=1
rtn-qgc-shell`). This is read once at startup — see
`GhostLaunchConfig::resolve` in `src/lib.rs`.

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
├── scripts/
│   ├── link-ghost.sh             # copy any ghost binary into binaries/ (dev dummy etc.)
│   ├── stage-ghost-bundle.sh     # stage the real host-portable ghost bundle (Wave 13)
│   └── docker-ghost-sidecar.sh   # superseded Wave 9-11 docker-wrapper stopgap (kept as dev fallback)
└── src-tauri/
    ├── Cargo.toml                # tauri v2 + tauri-plugin-shell
    ├── build.rs
    ├── tauri.conf.json           # devUrl :3000, externalBin binaries/ghost,
    │                             # bundle.resources ghost-resources/{lib,plugins},
    │                             # bundle targets (appimage+deb, nsis)
    ├── capabilities/default.json # shell sidecar permissions
    ├── icons/                    # app icons (required for compile + bundle)
    ├── binaries/                 # ghost-<triple> + ghost-resources/ (gitignored)
    └── src/
        ├── main.rs
        └── lib.rs                # sidecar spawn + env wiring + watchdog + ghost-status
```

## Bundling

`bundle.active` is `true` and `bundle.targets` is explicit:
`appimage` + `deb` on Linux, `nsis` on Windows (the bundler skips targets
that don't apply to the host OS).

```sh
cd web && bun run build:tauri   # -> ../tauri/dist
cd tauri/scripts && ./stage-ghost-bundle.sh
cd ../src-tauri
# LD_LIBRARY_PATH here is only so linuxdeploy's own ELF dependency scan
# (AppImage bundling) can resolve the ghost sidecar's Qt6 libs; it plays
# no role in how the *installed* app runs (that's the env lib.rs sets).
LD_LIBRARY_PATH="$PWD/target/release/lib" cargo tauri build --bundles deb,appimage
```

As of Wave 13 this has been exercised end to end (deb ~84M, AppImage
~129M — big because the ghost sidecar's bundled Qt6/ICU closure, ~110M,
ships as `bundle.resources` inside each installer) and verified: the
`.deb` installs cleanly on a bare `ubuntu:24.04` container with no prior
Qt, and the installed ghost binary serves the full bridge e2e; the
AppImage self-extracts (`--appimage-extract`) and its ghost binary does
the same when run manually or spawned by the shell under Xvfb. See
`git log` / wave notes for the full transcript. Remaining release gaps:
no code signing, x86_64-only (no aarch64 bundle), and the ghost binary
inside the bundle is not stripped (see
`src-tauri/binaries/README.md`/`ghost-bundle/MANIFEST.txt`).

## Notes

- The ghost contract is documented at the top of
  [`src-tauri/src/lib.rs`](src-tauri/src/lib.rs) — keep it in sync with
  the real headless QGC as it lands.
