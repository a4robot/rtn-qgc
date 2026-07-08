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
//! - Spawn:   `ghost --headless --bridge-port 8877`
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

use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc, Mutex,
};
use std::time::{Duration, Instant};

use serde::Serialize;
use tauri::{AppHandle, Emitter, RunEvent};
use tauri_plugin_shell::{
    process::{CommandChild, CommandEvent},
    ShellExt,
};

const GHOST_ARGS: [&str; 3] = ["--headless", "--bridge-port", "8877"];
const MAX_RESTARTS: u32 = 5;
/// If ghost stays up at least this long, the restart counter resets and
/// the next exit is treated as a fresh (first) crash.
const STABLE_UPTIME: Duration = Duration::from_secs(60);

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
) -> Result<(), tauri_plugin_shell::Error> {
    let (mut rx, child) = app.shell().sidecar("ghost")?.args(GHOST_ARGS).spawn()?;
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
        let mut attempt: u32 = 0;
        loop {
            let started = Instant::now();
            match run_ghost_once(&app, attempt, &slot).await {
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
