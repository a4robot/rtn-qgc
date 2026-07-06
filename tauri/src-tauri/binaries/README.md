# Ghost sidecar binaries

This directory holds the **ghost** sidecar — the headless QGroundControl
core the Tauri shell spawns and supervises. It is declared in
`tauri.conf.json` as `bundle.externalBin: ["binaries/ghost"]`.

Tauri resolves external binaries by **target triple**: the file must be
named `ghost-<target-triple>` (with `.exe` appended on Windows), e.g.

- `ghost-x86_64-unknown-linux-gnu`
- `ghost-aarch64-apple-darwin`
- `ghost-x86_64-pc-windows-msvc.exe`

Find your host triple with:

```sh
rustc -vV | sed -n 's/^host: //p'
```

## Dev: wiring the dummy ghost

Until the real headless QGC binary exists, use the dummy from
`tauri/dummy-ghost/dummy_ghost.sh`. It prints `ghost: listening on 8877`
and sleeps forever — enough for the shell's spawn/log/watchdog path to be
exercised end to end.

From the repo root:

```sh
TRIPLE=$(rustc -vV | sed -n 's/^host: //p')
cp tauri/dummy-ghost/dummy_ghost.sh "tauri/src-tauri/binaries/ghost-$TRIPLE"
chmod +x "tauri/src-tauri/binaries/ghost-$TRIPLE"
```

## Later: the real ghost

Drop the real headless QGC build here under the same
`ghost-<target-triple>` naming. It must honor the contract documented at
the top of `../src/lib.rs`:

```
ghost --headless --bridge-port 8877
```

serving its bridge on `127.0.0.1:8877` and logging to stdout/stderr.

`ghost-*` files are gitignored (see `tauri/.gitignore`) — binaries are
never committed; only this README lives in git.
