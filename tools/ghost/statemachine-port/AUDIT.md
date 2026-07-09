# QGCStateMachine API-Surface Audit (Wave 15 / M8 Q8a)

Audit + characterization baseline for **Q8b: port QGCStateMachine off
QStateMachine**. This is the prerequisite job — no porting or replacement
code lands in this wave. It answers two questions:

1. Of the ~90-file `QGCStateMachine` framework built on top of
   `Qt6::StateMachine`, what does the codebase **actually use**, versus what
   is unused framework surface?
2. What does a drop-in (or per-consumer-migrated) `QGCStateMachine`
   built on `QObject` + `std::` primitives need to implement?

Companion deliverable: `capture.sh` + `baseline-mocklink-connect.log`, a
deterministic MockLink transition-log baseline to replay-compare against
once the port lands (see "Characterization harness" below).

## Why this matters for M7/M8

Wave 14 (Q7d) found `Qt6::StateMachine` INTERFACE-links `Qt6::Gui` — so as
long as anything in the tree links the `StateMachine` Qt module,
`libQt6Gui` (and its DBus dependency) stays in the ghost's link graph, even
in the headless/no-QML "OFF" build. `QGCStateMachine` is the only thing in
this repo that pulls in `Qt6::StateMachine` (`CMakeLists.txt:226`). Cutting
that link is a hard M8 blocker.

## Consumer inventory (verified)

11 files outside `src/Utilities/StateMachine/` reference `QGCStateMachine`
or its States/Transitions:

| File | Role |
|---|---|
| `src/Vehicle/Vehicle.h` / `.cc` | Owns `_initialConnectStateMachine`, calls `start()`/`active()` only — **not** a direct state/transition author |
| `src/Vehicle/InitialConnectStateMachine.h` / `.cc` | `QGCStateMachine` subclass, the primary machine (8 states, linear + skip/retry) |
| `src/Vehicle/ComponentInformation/ComponentInformationManager.h` / `.cc` | `QGCStateMachine` subclass (6 states), drives 4 sequential requests through a reusable helper machine |
| `src/Vehicle/ComponentInformation/RequestMetaDataTypeStateMachine.h` / `.cc` | `QGCStateMachine` subclass (7 states); instantiated **once** as a `ComponentInformationManager` member and re-`request()`-ed 4x (not `SubMachineState`-composed) |
| `src/FactSystem/ParameterManager.cc` | Constructs two **ad-hoc, per-call** `QGCStateMachine` instances (`PARAM_SET`, `PARAM_REQUEST_READ`) directly with `new QGCStateMachine(...)`, no subclass |
| `src/WebBridge/MissionChannel.h` / `.cc` | **Not an actual consumer.** Two comments reference `InitialConnectStateMachine` by name/line number for context; no `#include`, no type usage. Confirmed with the wave-15 agent editing this file — nothing to migrate here. |

Vehicle.{h,cc} is a thin caller of the public API surface only
(`start()`, `active()`, `progressUpdate` signal) — it never touches
`QState`/`QAbstractTransition` directly and needs zero changes beyond
whatever `InitialConnectStateMachine`'s public surface becomes.

## Feature-surface matrix

Columns: **Offered** = present in the 90-file framework and documented in
`QGCStateMachine.h`. **Used** = actually exercised by one of the 5 real
consumer machines above (InitialConnectStateMachine,
ComponentInformationManager, RequestMetaDataTypeStateMachine,
ParameterManager's PARAM_SET, ParameterManager's PARAM_REQUEST_READ).

| Qt State Machine feature | Offered | Used | Notes |
|---|:---:|:---:|---|
| Flat `QState` children of one `QStateMachine` | ✔ | ✔ | **All 5 machines are flat** — one level of `QGCState`-derived children directly under the machine. |
| Nested/hierarchical `QState` (true HSM depth > 1) | ✔ (`createTimedActionState` builds a 2-level composite) | ✘ | `createTimedActionState()` is unused by any consumer. No consumer builds nested states. |
| Parallel states (`QState::ParallelStates` / `ParallelState`) | ✔ | ✘ | Zero consumer instantiations of `ParallelState`/`addParallelState`. |
| History states (`QHistoryState` / `QGCHistoryState`) | ✔ | ✘ | Zero consumer usage. |
| Sub-machines (`SubMachineState`, nested `QStateMachine`) | ✔ | ✘* | `ComponentInformationManager` owns a `RequestMetaDataTypeStateMachine` as a plain composed `QObject` member and calls `request()` on it 4x — this is manual composition, **not** `addSubMachineState()`/`SubMachineState`. `SubMachineState` itself has zero consumers. |
| Final states (`QFinalState` / `QGCFinalState`) | ✔ | ✔ | Every one of the 5 machines ends in exactly one `QGCFinalState`; `finished()` signal is relied on (`InitialConnectStateMachine::_signalComplete`, `RequestMetaDataTypeStateMachine`'s `requestComplete`). |
| Signal transitions (`QState::addTransition(sender, signal, target)`, `QGCSignalTransition`) | ✔ | ✔ | The dominant transition style — `WaitStateBase::completed`/`timedOut`, `QGCState::advance`/`error`, `SkippableAsyncState::skipped`. |
| Event-based transitions (`QStateMachine::postEvent`, `MachineEventTransition`, `NamedEventTransition`, `EventQueuedState`) | ✔ | ✘ | No consumer calls `postEvent`/`postDelayedEvent`/`addEventTransition`/`addEventQueuedState`. The `machineEvent(QString)` signal has no listeners outside the framework itself. |
| Guarded/conditional transitions (`GuardedTransition`, `addGuardedTransition`, `addConditionalTransition`, `ConditionalState`) | ✔ | ✘ | `ConditionalState` appears only as an unused forward-declaration in `RequestMetaDataTypeStateMachine.h`. Consumers implement "guards" as plain skip-predicate lambdas passed into `SkippableAsyncState`/`AsyncFunctionState` constructors, not via the transition-guard mechanism. |
| Self-loop / internal transitions (`addSelfLoopTransition`, `InternalTransition`) | ✔ | ✘ | Unused. |
| Retry transitions (`RetryTransition`, `addRetryTransition`) | ✔ | ✔ | `InitialConnectStateMachine::_wireTimeoutHandling()` is the **only** user, wiring `WaitStateBase::timedOut` → retry-then-advance for 6 of its 8 states. |
| Timeout transitions (`TimeoutTransition`, `addTimeoutTransition`) | ✔ | ✘ (indirectly ✔) | `addTimeoutTransition()` itself is unused; timeouts are instead built into `WaitStateBase`'s constructor (`timeoutMsecs` param → internal `WaitStateBase::timedOut` signal), which *is* heavily used. The framework has two parallel timeout mechanisms and only one is used. |
| Queued/deferred connections across threads | ✔ (implicit via Qt signal/slot) | ✔ | `Vehicle::sendMessageOnLinkThreadSafe`, MAVLink message delivery from link I/O threads into machine-owning thread rely on Qt's auto/queued connection semantics — this is load-bearing and must be preserved by any replacement (states react to signals emitted from other threads). |
| Initial-state semantics (`setInitialState`, auto-transition into initial state on `start()`) | ✔ | ✔ | Every machine sets exactly one initial state in its constructor before any `start()`. |
| `started`/`stopped`/`finished` machine signals | ✔ | ✔ | `finished()` is relied on for completion signaling in 3 of 5 machines (chained to `Vehicle::initialConnectComplete`, `RequestMetaDataTypeStateMachine::requestComplete`, and the base class's progress-completion logic). `started`/`stopped` are logged but not otherwise consumed by business logic. |
| `QStateMachine::configuration()` / active-state introspection | ✔ (`activeStates()`, `isStateActive()`) | ✘ | No consumer queries active configuration directly (some framework helpers like `dumpConfiguration()`/`exportAsDot()` use it internally, but no consumer calls those either). |
| Property-restore policy (`QState::RestoreProperties`, `enablePropertyRestore`) | ✔ | ✘ | Unused. |
| Global/local error-state wiring (`setGlobalErrorState`, `QGCState::setLocalErrorState`, `registerState`) | ✔ | ~ | `registerState()` is called (it's baked into every `add*State()` factory and the base-class constructors), but **no consumer ever calls `setGlobalErrorState()`**, so it's always a no-op in practice — the "error" transitions consumers actually wire are explicit `addThisTransition(&QGCState::error, ...)` calls (see ParameterManager), not the global-error-state mechanism. |
| `StateContext` (typed inter-state data bag) | ✔ | ✘ | Zero consumer calls to `context()`/`context().set/get`. Consumers pass data via C++ lambda captures and member variables instead. |
| `StateHistoryRecorder` / `StateMachineProfiler` / `StateMachineLogger` (the "profiler/logger helpers") | ✔ | ~ (opt-in, off by default) | Never enabled programmatically by any consumer. Wired to 3 env vars in `QGCStateMachine`'s constructor: `QGC_STATEMACHINE_HISTORY`, `QGC_STATEMACHINE_PROFILE`, `QGC_STATEMACHINE_LOG` — **this is exactly the hook the Q8a characterization harness uses** (see below), so it's "used" only by tooling, never by product code. |

**Bottom line:** real usage is a small, flat subset of what the framework
offers: one level of states, signal transitions between them (`completed`/
`timedOut`/`skipped`/`advance`/`error`), `WaitStateBase`'s built-in
per-state timeout with a single `addRetryTransition` retry pattern, and
`QGCFinalState`/`finished()` for completion signaling. None of parallel
states, history states, true sub-machine composition, event-posting,
guarded/self-loop/internal transitions, `StateContext`, or property-restore
are exercised anywhere in product code.

## State/Transition class usage census

Of the 90 files, usage by class (✔ = instantiated or subclassed by a real
consumer; counts are distinct call sites, not files):

**Used:** `RetryableRequestMessageState` (1: autopilot version), `AsyncFunctionState` (5), `SkippableAsyncState` (7), `RetryState` (1: `SignalComplete`, and `ParameterManager` doesn't use it — see below), `QGCFinalState` (5, one per machine), `FunctionState` (4), `WaitForParamResponseState` (2, both `ParameterManager` machines), `SendMavlinkMessageState` (1, `PARAM_SET`), `WaitStateBase` (base of the above — used transitively), `QGCState`/`QGCAbstractState` (bases — used transitively), `ErrorRecoveryBuilder`/`addErrorRecoveryState` (1, `RequestMetaDataTypeStateMachine`'s `CompleteRequest`), `QGCSignalTransition` (base of `RetryTransition`, used transitively), `RetryTransition`/`addRetryTransition` (6 call sites, all in `InitialConnectStateMachine`).

**Unused by any consumer** (framework-only or truly dead): `ParallelState`, `QGCHistoryState`, `SubMachineState`, `ConditionalState`, `DelayState`, `EventQueuedState`, `ShowAppMessageState`, `CircuitBreakerState`, `FallbackChainState`, `RollbackState`, `LoopState`, `SequenceState`, `ProgressState`, `WaitForSignalState`, `WaitForMavlinkMessageState`, `SendMavlinkCommandState`, `GuardedTransition`, `InternalTransition`, `MachineEventTransition`, `NamedEventTransition`, `QGCEventTransition`, `SignalDataTransition`, `TimeoutTransition`/`addTimeoutTransition`, `StateContext`, `createTimedActionState`.

That's roughly **24 of the ~35 States/Transitions classes are dead weight**
from the real-consumer perspective — they exist to support a generality the
codebase doesn't currently use. This matters directly for Q8b sizing: a
literal 1:1 reimplementation of all 90 files on top of `QObject` is far more
work than reimplementing only the ~11 classes actually load-bearing.

## Minimal-replacement design spec

**Recommendation: drop-in replacement of the *used* surface, not a
per-consumer migration.** All 5 consumers share the same shape (flat states,
signal transitions, per-state timeout, retry, final-state completion) and
all go through the same `QGCStateMachine` base class API
(`addFunctionState`, `addAsyncFunctionState`, `addRetryTransition`,
`setInitialState`, `start`, `finished` signal, `registerState`). A
same-named/same-shaped `QGCStateMachine` class built on `QObject` instead of
`QStateMachine` lets all 11 consumer files stay unchanged at the call site;
only `src/Utilities/StateMachine/` internals change. This avoids a 5-way
parallel migration and matches "Match the style of code you're editing" —
consumers already treat `QGCStateMachine` as their own abstraction, not as
`QStateMachine` directly (only the framework's own `.cc` files and 2
consumer `.cc` files — `RequestMetaDataTypeStateMachine.cc`,
`InitialConnectStateMachine.cc` — touch raw `QStateMachine`/`QState`/
`QSignalTransition` types, and always through inherited/framework-provided
methods, never by including `<QtStateMachine/...>` directly themselves).

A replacement `QGCStateMachine` (QObject-based) must implement:

1. **State registry & hierarchy (flat only, in v1):** an ordered list of
   states, each with `objectName()`, entry/exit callbacks. No nested-state
   support is required to satisfy current consumers — but `QGCAbstractState`
   already models entry/exit as virtual `onEntry()`/`onExit()` callbacks and
   that shape can stay identical (only the `QState` base disappears — see
   next section for how to reproduce this on `QObject`).
2. **`start()` / initial-state semantics:** on `start()`, transition
   directly into the state set via `setInitialState()`, emit
   `started`/`runningChanged`.
3. **Explicit transition graph, signal-driven:** replace `QState::
   addTransition(sender, signal, target)` with an explicit registration:
   `connect(sender, signal, [this]{ _transitionTo(target); })` wired by the
   same-named `addTransition`/`addThisTransition`/`addRetryTransition`
   builder methods QGCStateMachine already exposes — this is a mechanical,
   same-API reimplementation since it's a thin wrapper around `QObject::
   connect` today anyway (see `QGCSignalTransition` — it already `connect()`s
   under the hood; the *transition object* itself (`QAbstractTransition`
   subclass, with `eventTest()`/`onTransition()`) is what disappears, not
   the connect-a-signal-to-advance-state idea).
4. **Per-state timeout:** `WaitStateBase` already implements this with a
   plain `QTimer` today (it does not lean on Qt's state-machine
   `TimeoutTransition` machinery) — this class needs no behavioral change,
   only its `QState`/`QGCState` base needs replacing.
5. **`finished()` signal on reaching the final state:** trivial —
   `QGCFinalState::onEntry()` already just emits a signal chain; replace the
   `QFinalState` base with a marker + explicit machine-side check.
6. **`configuration()` / `isStateActive()` / `activeStates()`:** track
   "current state(s)" as a simple `QSet<QAbstractState*>` maintained by the
   machine itself on every transition (already effectively what `QState-
   Machine::configuration()` returns for a flat topology with no parallel
   regions) — cheap to reproduce exactly.
7. **Progress tracking, timeout overrides/stats, dot export, dead-end/
   unreachable-state analysis, `StateHistoryRecorder`/`StateMachineProfiler`/
   `StateMachineLogger`:** all of this is pure `QGCStateMachine`-level
   bookkeeping already implemented independently of `QStateMachine` guts
   (they use `findChildren<QAbstractState*>()` and the `entered`/`exited`
   signals, which a replacement must still emit) — **port as-is**, no
   redesign needed, just re-point at whatever the new base class emits.
8. **Threading:** must preserve today's implicit behavior — signals
   delivered from other threads (link I/O) correctly queue onto the
   machine's thread via Qt's connection-type auto-detection. A `QObject`-
   based (not `QStateMachine`-based) implementation gets this for free as
   long as the replacement classes remain `QObject` subclasses living on the
   Vehicle's thread, which they already are.

**What can be dropped, not reimplemented, for v1** (nothing currently
depends on it — confirm again at Q8b time by re-running this audit's grep
census against HEAD): `ParallelState`, `QGCHistoryState`, `SubMachineState`,
`ConditionalState`/guarded transitions as a first-class transition type
(guards can stay as skip-predicate lambdas, which is how every consumer
already uses them), `StateContext`, event-posting (`postEvent`/
`MachineEventTransition`/`EventQueuedState`), property-restore,
`TimeoutTransition`/`addTimeoutTransition`, self-loop/internal transitions,
`DelayState`, `ShowAppMessageState`, `CircuitBreakerState`,
`FallbackChainState`, `RollbackState`, `LoopState`, `SequenceState`,
`ProgressState`, `WaitForSignalState`, `WaitForMavlinkMessageState`,
`SendMavlinkCommandState`, `createTimedActionState`. If Q8b wants to keep
API compatibility for future consumers, these can be stubbed to
compile-but-assert/qCCritical rather than fully reimplemented, shrinking the
port from ~90 files to roughly the dozen files backing the used-feature
list above, plus the 3 diagnostic helpers (history/profiler/logger) ported
mechanically.

## Sizing

Confirms the M8 roadmap's **L** sizing for Q8b, but with a concrete lower
bound now available: the mechanical, same-API replacement of ~12
load-bearing classes (`QGCStateMachine`, `QGCAbstractState`, `QGCState`,
`QGCFinalState`, `WaitStateBase`, `AsyncFunctionState`,
`SkippableAsyncState`, `RetryableRequestMessageState`, `RetryState`,
`FunctionState`, `SendMavlinkMessageState`, `WaitForParamResponseState`,
`ErrorRecoveryBuilder`, `RetryTransition`/`QGCSignalTransition`) plus the 3
diagnostic helpers, versus stub/delete for the other ~24+ unused classes —
is meaningfully smaller than porting all 90 files, but every one of the 5
consumer machines needs a careful transition-log replay-compare against
this wave's baseline (Q8a's other deliverable) since the flat-signal-
transition graph is exactly the part being hand-rewritten without Qt's
state machine engine driving it.

## Characterization harness (transition-log capture)

### What it hooks

No source changes were needed. `QGCStateMachine`'s constructor
(`src/Utilities/StateMachine/QGCStateMachine.cc:40-48`) already wires three
env vars to its built-in diagnostics:

- `QGC_STATEMACHINE_HISTORY` → `StateHistoryRecorder` (circular buffer, not
  console-logged by default — not used by this harness)
- `QGC_STATEMACHINE_PROFILE` → `StateMachineProfiler` (timing only — not
  used by this harness)
- `QGC_STATEMACHINE_LOG=1` → `StateMachineLogger`, which connects to every
  state's `entered`/`exited` signals and logs via
  `qCDebug(QGCStateMachineLog)` (category `Utilities.QGCStateMachine`)

Combined with the hand-written `qCDebug(...)` call sites already present in
`InitialConnectStateMachine.cc`, `ComponentInformationManager.cc`,
`RequestMetaDataTypeStateMachine.cc`, and `ParameterManager.cc`, this is
enough to reconstruct a full transition trace with zero code changes.

### The category-filter dead end (documented so it isn't re-walked)

Getting the qCDebug output to actually print took two failed attempts,
both left as comments in `capture.sh`:

1. `QGC_LOG_LEVEL=debug` (the obvious "just turn on all debug logging"
   knob, per `LogManager::applyEnvironmentLogLevel()`) unmutes *every*
   debug category process-wide (gstreamer, qt.network.http2, plugin
   loader, ...). That firehose is heavy enough on the main thread that
   MockLink's tick timer and the WebBridge never got a chance to run —
   observed 40+ seconds with zero `tick` messages delivered over the
   WebSocket, where the same boot completes in ~1s without it.
2. Qt's native `QT_LOGGING_RULES` env var, scoped to just the 5 relevant
   categories, has **no effect at all**. `QGCLoggingCategoryManager::
   installFilter()` (called unconditionally from `QGCApplication::init()`,
   GUI or headless) replaces Qt's category filter function wholesale;
   its `_categoryFilter()` decides enabled/disabled for any non-`"qt.*"`
   category purely from QGC's own `_categoryLevels` map (populated from
   `QSettings` + the `--logging` CLI flag), never consulting
   `QLoggingCategory`'s rules engine. (It *does* chain to the previous
   filter for `"qt.*"` categories, which is why (1) still affected
   `qt.network.http2` etc.)

The supported hook is the `--logging <comma-separated-categories>` CLI flag
(`QGCCommandLineParser`'s `kOptLogging`), which feeds
`QGCLoggingCategoryManager::installFilter(commandLineLoggingOptions)` and
unmutes exactly the named categories at `QtDebugMsg`. `capture.sh` passes:

```
--logging Utilities.QGCStateMachine,Vehicle.InitialConnectStateMachine,ComponentInformation.ComponentInformationManager,ComponentInformation.RequestMetaDataTypeStateMachine,FactSystem.ParameterManager
```

### Files

- `capture-probe.ts` — bun/WS probe. Connects, waits for the MockLink
  vehicle (128) to tick, subscribes telemetry (this alone drives
  `InitialConnectStateMachine` → `ComponentInformationManager` →
  `RequestMetaDataTypeStateMachine` x4 to completion automatically — no
  probe action needed), then explicitly drives `ParameterManager`'s
  `PARAM_SET` machine via a `setParam` WebBridge command (`FactChannel`).
  **Note:** the round-trip must change the value — `Fact::setRawValue()`
  (`src/FactSystem/Fact.cc:128`) is a silent no-op when the new value
  equals the current one (no MAVLink send, no state machine, no response),
  so the probe nudges the fetched value by `+1` rather than echoing it back
  verbatim.
- `capture.sh` — boots the existing headless ("OFF"/no-QML) ghost binary
  against `--mock-link`, drives the probe, greps the container log for the
  5 relevant categories, and normalizes it (strips the leading
  process-relative timestamp, `StateMachineLogger`'s own internal
  `[+seconds]` elapsed marker, and `0x...` object-pointer addresses — all
  three vary run to run and carry no transition-order information).
  Usage: `./capture.sh [output-file] [run-label]`.
- `baseline-mocklink-connect.log` — the normalized baseline, 223 lines,
  covering: `InitialConnectStateMachine`'s full run (autopilot version →
  standard modes → comp-info → parameters → mission → geofence → rally →
  complete → final) with `ComponentInformationManager`'s 4 sequential
  `RequestMetaDataTypeStateMachine` runs nested inside its `RequestCompInfo`
  state (general/param/events/actuators — events and actuators are
  skipped, MockLink's `CompInfoGeneral` doesn't advertise support for
  them), and one `ParameterManager` `PARAM_SET` machine run (driven
  explicitly by the probe).

### Determinism report

Ran 3x back-to-back (fresh container each run, `qgc-build-w14gui-off`
binary, MockLink). **All 3 normalized captures are byte-identical**
(223/223 lines, `diff` empty pairwise). No nondeterministic transition
ordering was observed in this configuration — the flows exercised here
(connect handshake → comp-info download → single explicit param set) have
no concurrent/racing signal sources feeding the same machine, so there was
nothing to produce a race. **Caveat for Q8b's replay-compare:** this
baseline does *not* exercise `InitialConnectStateMachine`'s retry-on-
timeout paths (`addRetryTransition`, `_wireTimeoutHandling`) or
`ParameterManager`'s `PARAM_REQUEST_READ` machine (see below) — both are
plausible sources of run-to-run nondeterminism (real timeouts race against
real responses) that this capture didn't need to tolerate because MockLink
responds well within every configured timeout. If Q8b's replay-compare
wants coverage of the retry paths, it will need a fault-injection variant
of this harness (e.g. a MockLink mode that drops the first
`AUTOPILOT_VERSION`/`COMPONENT_METADATA` response) — out of scope for this
wave's baseline but flagged for Q8b.

### Known coverage gap: `ParameterManager`'s `PARAM_REQUEST_READ` machine

`ParameterManager::_mavlinkParamRequestRead()` (backing the
`PARAM_REQUEST_READ` `QGCStateMachine`, `ParameterManager.cc:925-1045`) is
**not** on the bulk-startup-download path. Startup parameter loading goes
through `_startParameterDownload()` → either `_requestHashCheck()` (raw
`PARAM_REQUEST_READ` MAVLink message, no state machine) or a raw
`PARAM_REQUEST_LIST` broadcast — neither uses `QGCStateMachine`. The
`PARAM_REQUEST_READ` machine only fires via `refreshParameter()`/
`refreshParametersPrefix()`/`bulkRefresh()`, which are reachable from UI
actions (e.g. "refresh parameter") but **not exposed over the current
WebBridge `FactChannel` protocol** (`getParam` reads the cached `Fact`
value directly, no MAVLink round trip). This capture harness therefore
exercises `PARAM_SET` but not `PARAM_REQUEST_READ`. Options for Q8b:
extend `FactChannel` with a `refreshParam` command (small, real feature,
arguably useful beyond this port), or add a dedicated CLI/test-only hook.
Flagging rather than building it this wave — out of the stated scope
(WebBridge channel logic is being edited by another wave-15 agent).

## Files added this wave

- `tools/ghost/statemachine-port/AUDIT.md` — this document
- `tools/ghost/statemachine-port/capture.sh` — capture harness (executable)
- `tools/ghost/statemachine-port/capture-probe.ts` — bun/WS probe driving the flows
- `tools/ghost/statemachine-port/baseline-mocklink-connect.log` — normalized 3x-reproduced baseline

No files under `src/` were modified. No rebuild was required — the
existing `qgc-build-w14gui-off` binary (built from wave-14 HEAD, and HEAD
at audit time is only a docs-only commit ahead of that build — verified
`git show --stat` touches only `STRANGLER_MILESTONES.md`) was reused
per the job's own escape valve ("prefer log-category capture over code
changes if the existing logging suffices").

## Q8b results (wave 16)

The port landed at `src/Utilities/StateMachine/portable/` (34 files: the
~12 load-bearing classes plus the 3 diagnostic helpers this doc's
minimal-replacement spec called out), selected by
`QGC_ENABLE_QT_STATEMACHINE` (default ON = this doc's 90-file framework,
untouched; OFF = portable/). See `src/Utilities/StateMachine/CMakeLists.txt`
and `portable/QGCStateMachine.h`'s header comment for the design.

**ldd**: `libQt6StateMachine.so` is gone from the OFF binary's link graph, as
predicted. `libQt6Gui.so`/`libQt6DBus.so` are *still* present, but not
because of this subsystem — `src/Utilities/Geo/CMakeLists.txt`'s
`QGCGeoMath` target links `Qt6::Gui` unconditionally (not `QGC_ENABLE_QML`-
gated) because `QGCGeo.cc`/`.h` uses `QVector3D` (a Qt6::Gui type despite
being pure 3-float-vector math) for headless-reachable ENU/ECEF coordinate
conversions. This was previously masked by `Qt6::StateMachine`'s own Gui
pull; eliminating it is a distinct follow-up job. Remaining Qt libs on the
OFF binary: Core, DBus, Gui, Network, Positioning, SerialPort (7→6, not the
theoretical 7→4, precisely because of the QGCGeoMath finding above).

**Replay-compare**: byte-identical against `baseline-mocklink-connect.log`
after one *additional*, documented normalization on top of `capture.sh`'s
existing one: the trailing `` - (Class::Method:line)`` context suffix
(`LogManager`'s `%{function}:%{line}` message pattern) is stripped for
lines in the `Utilities.QGCStateMachine` category only. This category's
backing source files are exactly what this port rewrites, so their
`__PRETTY_FUNCTION__`/line-number metadata necessarily differs from the
Qt-based original's even when the class name, method name, category,
message text, and — critically — the *order* of every line are identical;
verifying it word-for-word would be verifying source-file layout, not
transition semantics. Every *other* category's lines (originating from the
five unchanged consumer files) matched the baseline byte-for-byte with no
normalization needed. Verified over 5 consecutive runs post-fix (see
below), all byte-identical after normalization.

**A genuine ordering bug, found and fixed**: an early version of this port
posted one fresh `Qt::QueuedConnection` dispatch per transition. That
introduced two problems, both traced empirically (not by inspection alone):
1. ParameterManager's ad-hoc `PARAM_SET` machine's `WaitForParamResponseState`
   would spuriously time out against MockLink's response in a large fraction
   of runs (MockLink runs its own `QThread` worker) — because each of the
   chain's trivial hops (`SendMavlinkMessageState` → a `FunctionState` →
   `WaitForParamResponseState`) was a fresh trip through the *generic* Qt
   event queue, giving MockLink's cross-thread traffic more opportunities to
   interleave ahead of the listener being armed than the Qt-based original's
   internal event processing (which drains a chain like this without
   yielding back to the outer event loop between hops).
2. The fix — draining all currently-queued transition steps in a loop
   instead of re-posting per hop — initially used a *per-instance* queue,
   which broke `ComponentInformationManager`'s completion ordering relative
   to `InitialConnectStateMachine`'s next transition (two *different*
   machine instances triggering each other mid-chain need one global FIFO,
   not two independently-draining local ones). Fixed by making the pending-
   step queue process-wide (`QGCStateMachine::_scheduleStep`/`_drainSteps`,
   static, shared by every instance) — see `portable/QGCStateMachine.cc`'s
   `qgcStateMachinePendingSteps()` for the detail and rationale.

**Conformance/probe**: `tools/ghost/mockghostprobe.ts` 6/6.
`tools/ghost/conformance/run.ts` 68-69/69 PASS (69 total minus the 2
environment-gated SKIPs — `--bridge-token` and the video group — both
expected without a second auth instance / GST feed), 0 FAIL, 0 GAP, run
against the OFF binary.

**Unit tests**: `test/Utilities/StateMachine/` (the Qt-based framework's
own suite — `QGCStateMachineTest`, per-state/per-transition tests including
several for classes this port intentionally excludes, e.g.
`GuardedTransitionTest`, `ParallelStateTest`, `TimeoutTransitionTest`) is
gated behind `QGC_ENABLE_QT_STATEMACHINE` too now, so it only compiles in
the ON config — porting that coverage to the portable framework is out of
this v1 port's scope (flagged, not silently dropped; see
`test/Utilities/StateMachine/CMakeLists.txt`'s comment). Not build-verified
under `QGC_BUILD_TESTING=ON` this wave (ON-path files are unmodified, and
`QGC_BUILD_TESTING` defaults OFF for Release builds either way) — flagged
for whoever next touches this to confirm with a Debug/testing-enabled
build.
