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

### Wave 12: host-portable ghost bundle exists — not yet wired

`tauri/scripts/docker-ghost-sidecar.sh` is a **dev-only** stopgap: it
shells out to `docker run qgc-build-ubuntu ...` because, until Wave 12,
the Release ghost binary only ran inside that image (host lacked Qt
6.10, and the host's system Qt6 6.4.2 is ABI-incompatible).

Wave 12 produced a Docker-free, host-runnable bundle at
`/home/bpasu/ghost-bundle/` (outside the repo — it's a build artifact,
never committed): `bin/QGroundControl` plus the exact `lib/` and
`plugins/` closure the binary needs (16 Qt6 libs + ICU, since the host's
system Qt6 is a different/incompatible version; GStreamer is *not*
bundled — host and build-image GStreamer 1.24.x package versions match
exactly), and a `ghost.sh` launcher that sets `LD_LIBRARY_PATH` /
`QT_PLUGIN_PATH` and execs the binary. See
`/home/bpasu/ghost-bundle/MANIFEST.txt` for the full inventory and
verified host requirements (glibc >= 2.38, GStreamer 1.x base/good/bad/
ugly/libav plugins via apt, x86_64). It passes the same telemetry +
video e2e probes used elsewhere in this repo, run directly on the host
with no Docker.

**This is not wired into the Tauri sidecar yet** (that's Wave 13). The
gap: `link-ghost.sh` and the `externalBin` contract above expect a
single executable file named `ghost-<target-triple>`, but the bundle is
a directory (binary + ~65M of libs + plugins) — it can't be dropped in
as-is. The likely Wave 13 shape, so this doesn't need re-deriving:

1. `tar czf ghost-bundle-<triple>.tar.gz -C /home/bpasu ghost-bundle`
   and ship that tarball as a build input (or re-produce the bundle
   in CI via the same docker-cp recipe used in Wave 12).
2. Ship `lib/` + `plugins/` as Tauri `bundle.resources` (not
   `externalBin`) so they land alongside the app at a resolvable
   resource path at install time.
3. Ship `bin/QGroundControl` itself (renamed) as the
   `binaries/ghost-<target-triple>` `externalBin` sidecar.
4. When spawning the sidecar (`Command::sidecar` in `../src/lib.rs`),
   set `LD_LIBRARY_PATH` / `QT_PLUGIN_PATH` env vars pointing at the
   resolved resource dir from step 2 — this replaces what `ghost.sh`
   does by hand, since Tauri's `Command` builder can set env directly
   and a shell-script wrapper as the sidecar adds its own packaging
   complications (`.sh` isn't an `externalBin`-friendly artifact on
   Windows).
5. `QT_QPA_PLATFORM=offscreen` should be set the same way (`ghost.sh`
   defaults to it; the sidecar spawn should too, since installed
   end-user machines won't reliably have a usable X/Wayland display for
   this headless core).

None of this was built in Wave 12 — intentionally out of scope (no
cargo/tauri builds, no bundle committed to git). This section only
exists so Wave 13 doesn't have to re-discover the Qt-ABI-mismatch /
GStreamer-version-match reasoning from scratch.
