//! RTN QGroundControl — Tauri shell.
//!
//! # Ghost sidecar contract
//!
//! "ghost" is the headless QGroundControl core, bundled as an external
//! binary (see `bundle.externalBin` in `tauri.conf.json`, resolved as
//! `binaries/ghost-<target-triple>`). This shell is a process manager and
//! window host ONLY — all application logic lives in the ghost core and
//! the web UI it talks to.
//!
//! - Spawn:   `ghost --headless --bridge-port 8877` (plus `--mock-link`
//!   iff `RTN_GHOST_MOCK=1` is set in the shell's own environment — see
//!   [`GhostLaunchConfig::resolve`]. Production installs spawn with no
//!   env var set, i.e. no `--mock-link`.)
//! - Env: `LD_LIBRARY_PATH` / `QT_PLUGIN_PATH` are pointed at the
//!   bundled Qt6 lib/plugin closure shipped as `bundle.resources`
//!   (`binaries/ghost-resources/{lib,plugins}` at build time, resolved
//!   at runtime via `app.path().resource_dir()`), and
//!   `QT_QPA_PLATFORM=offscreen` is forced — installed end-user
//!   machines aren't guaranteed a usable X/Wayland display for this
//!   headless core. See `binaries/README.md` for why the ghost binary
//!   cannot simply rely on the host's system Qt.
//! - Stdout/stderr: log lines, forwarded verbatim into the shell log
//!   under the `ghost` target.
//! - Bridge:  ghost serves its WebSocket/HTTP bridge on
//!   `127.0.0.1:8877`. The web UI (Bun dev server on :3000 during dev)
//!   connects to it directly; the shell never touches the bridge.
//! - Exit:    any exit is treated as a crash. The watchdog restarts
//!   ghost with exponential backoff (1, 2, 4, 8, 16 s — max 5
//!   consecutive attempts), then gives up. Every transition is emitted
//!   to the webview as a `ghost-status` event with payload
//!   `{ "state": "running" | "restarting" | "dead", "attempt": n }`.

use std::ffi::OsString;
use std::path::PathBuf;
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc, Mutex,
};
use std::time::{Duration, Instant};

use serde::Serialize;
use tauri::{AppHandle, Emitter, Manager, RunEvent};
use tauri_plugin_shell::{
    process::{CommandChild, CommandEvent},
    ShellExt,
};

const GHOST_BASE_ARGS: [&str; 3] = ["--headless", "--bridge-port", "8877"];
/// When set to "1" in the shell's own environment, ghost is spawned with
/// an extra `--mock-link` argument so the MockLink vehicle (128) is
/// available without a real vehicle connection. This is a *dev/demo*
/// toggle only — unset (the default) means a production spawn with no
/// mock link.
const MOCK_LINK_ENV: &str = "RTN_GHOST_MOCK";
const MAX_RESTARTS: u32 = 5;
/// If ghost stays up at least this long, the restart counter resets and
/// the next exit is treated as a fresh (first) crash.
const STABLE_UPTIME: Duration = Duration::from_secs(60);

/// Resolved once at startup: the args ghost is spawned with, and the env
/// vars that point it at its bundled Qt6 lib/plugin closure. Every
/// restart attempt reuses this — none of it changes at runtime.
struct GhostLaunchConfig {
    args: Vec<String>,
    ld_library_path: Option<OsString>,
    qt_plugin_path: Option<OsString>,
}

impl GhostLaunchConfig {
    fn resolve(app: &AppHandle) -> Self {
        let mut args: Vec<String> = GHOST_BASE_ARGS.iter().map(|s| s.to_string()).collect();
        if std::env::var(MOCK_LINK_ENV).as_deref() == Ok("1") {
            log::warn!(
                target: "ghost",
                "{MOCK_LINK_ENV}=1 — spawning ghost with --mock-link (dev/demo only, not for production)"
            );
            args.push("--mock-link".to_string());
        }

        let (ld_library_path, qt_plugin_path) = match app.path().resource_dir() {
            Ok(dir) => (
                Some(prepend_path(dir.join("lib"), "LD_LIBRARY_PATH")),
                Some(prepend_path(dir.join("plugins"), "QT_PLUGIN_PATH")),
            ),
            Err(err) => {
                log::error!(
                    target: "ghost",
                    "failed to resolve resource dir: {err} — ghost will run without its bundled \
                     Qt6 libs/plugins and will likely fail to start (see binaries/README.md)"
                );
                (None, None)
            }
        };

        Self {
            args,
            ld_library_path,
            qt_plugin_path,
        }
    }
}

/// Builds `<dir>[:<existing $VAR>]`, preserving anything already present
/// in the shell's own environment for that variable (defense in depth,
/// mirroring what `ghost-bundle/ghost.sh` does by hand for local dev).
fn prepend_path(dir: PathBuf, existing_env_var: &str) -> OsString {
    match std::env::var_os(existing_env_var) {
        Some(existing) if !existing.is_empty() => {
            let mut joined = dir.into_os_string();
            joined.push(":");
            joined.push(existing);
            joined
        }
        _ => dir.into_os_string(),
    }
}

#[derive(Clone, Serialize)]
struct GhostStatus {
    state: &'static str,
    attempt: u32,
}

/// Shared handle between the watchdog and the app's exit path: the live
/// sidecar child (so shutdown can kill it — without this the ghost
/// outlives the shell) and a flag telling the watchdog that an exit-time
/// termination is intentional, not a crash to restart.
#[derive(Default)]
struct GhostSlot {
    child: Mutex<Option<CommandChild>>,
    shutting_down: AtomicBool,
}

fn emit_status(app: &AppHandle, state: &'static str, attempt: u32) {
    log::info!(target: "ghost", "status: {state} (attempt {attempt})");
    if let Err(err) = app.emit("ghost-status", GhostStatus { state, attempt }) {
        log::error!(target: "ghost", "failed to emit ghost-status: {err}");
    }
}

/// Spawn ghost once, emit "running", and pump its output into the log.
/// Returns when the process terminates; errors if the spawn itself fails.
async fn run_ghost_once(
    app: &AppHandle,
    attempt: u32,
    slot: &GhostSlot,
    launch: &GhostLaunchConfig,
) -> Result<(), tauri_plugin_shell::Error> {
    let mut cmd = app.shell().sidecar("ghost")?.args(&launch.args);
    if let Some(ld_library_path) = &launch.ld_library_path {
        cmd = cmd.env("LD_LIBRARY_PATH", ld_library_path);
    }
    if let Some(qt_plugin_path) = &launch.qt_plugin_path {
        cmd = cmd.env("QT_PLUGIN_PATH", qt_plugin_path);
    }
    // Installed end-user machines aren't guaranteed a usable X/Wayland
    // display; this headless core doesn't need one.
    cmd = cmd.env("QT_QPA_PLATFORM", "offscreen");

    let (mut rx, child) = cmd.spawn()?;
    *slot.child.lock().unwrap() = Some(child);
    emit_status(app, "running", attempt);

    while let Some(event) = rx.recv().await {
        match event {
            CommandEvent::Stdout(line) => {
                log::info!(target: "ghost", "{}", String::from_utf8_lossy(&line).trim_end());
            }
            CommandEvent::Stderr(line) => {
                log::warn!(target: "ghost", "{}", String::from_utf8_lossy(&line).trim_end());
            }
            CommandEvent::Error(err) => {
                log::error!(target: "ghost", "process error: {err}");
            }
            CommandEvent::Terminated(payload) => {
                log::warn!(
                    target: "ghost",
                    "terminated (code: {:?}, signal: {:?})",
                    payload.code,
                    payload.signal
                );
                break;
            }
            _ => {}
        }
    }
    // The child is gone (or being torn down at exit) — drop the handle so
    // shutdown never kills a stale pid.
    slot.child.lock().unwrap().take();
    Ok(())
}

/// Watchdog: keep ghost alive, restarting with exponential backoff.
fn spawn_watchdog(app: AppHandle, slot: Arc<GhostSlot>) {
    tauri::async_runtime::spawn(async move {
        let launch = GhostLaunchConfig::resolve(&app);
        let mut attempt: u32 = 0;
        loop {
            let started = Instant::now();
            match run_ghost_once(&app, attempt, &slot, &launch).await {
                Ok(()) if started.elapsed() >= STABLE_UPTIME => attempt = 0,
                Ok(()) => {}
                Err(err) => log::error!(target: "ghost", "failed to spawn: {err}"),
            }

            if slot.shutting_down.load(Ordering::SeqCst) {
                log::info!(target: "ghost", "shutdown in progress — not restarting");
                return;
            }

            attempt += 1;
            if attempt > MAX_RESTARTS {
                log::error!(target: "ghost", "giving up after {MAX_RESTARTS} restarts");
                emit_status(&app, "dead", MAX_RESTARTS);
                return;
            }

            let delay = Duration::from_secs(1 << (attempt - 1)); // 1, 2, 4, 8, 16 s
            log::warn!(
                target: "ghost",
                "restarting in {delay:?} (attempt {attempt}/{MAX_RESTARTS})"
            );
            emit_status(&app, "restarting", attempt);
            tokio::time::sleep(delay).await;
        }
    });
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let ghost = Arc::new(GhostSlot::default());
    let ghost_for_setup = ghost.clone();

    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(
            tauri_plugin_log::Builder::new()
                .level(log::LevelFilter::Info)
                .build(),
        )
        .setup(move |app| {
            spawn_watchdog(app.handle().clone(), ghost_for_setup.clone());
            Ok(())
        })
        .build(tauri::generate_context!())
        .expect("error while building tauri application")
        .run(move |_app, event| {
            // Without this the sidecar outlives the shell (observed as an
            // orphaned docker container with the wrapper sidecar). Flag the
            // watchdog first so the resulting Terminated isn't "restarted".
            if matches!(event, RunEvent::ExitRequested { .. } | RunEvent::Exit) {
                ghost.shutting_down.store(true, Ordering::SeqCst);
                if let Some(child) = ghost.child.lock().unwrap().take() {
                    log::info!(target: "ghost", "shutdown: killing sidecar");
                    if let Err(err) = child.kill() {
                        log::error!(target: "ghost", "failed to kill sidecar: {err}");
                    }
                }
            }
        });
}
