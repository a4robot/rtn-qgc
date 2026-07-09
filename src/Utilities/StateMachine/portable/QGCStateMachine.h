#pragma once

#include "AsyncFunctionState.h"
#include "ErrorRecoveryBuilder.h"
#include "FunctionState.h"
#include "QGCAbstractState.h"
#include "QGCFinalState.h"
#include "QGCState.h"
#include "RetryableRequestMessageState.h"
#include "RetryState.h"
#include "RetryTransition.h"
#include "SendMavlinkMessageState.h"
#include "SkippableAsyncState.h"
#include "StateContext.h"
#include "WaitForParamResponseState.h"
#include "WaitStateBase.h"

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QPair>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <functional>

class Vehicle;
class StateMachineLogger;
class StateMachineProfiler;
class StateHistoryRecorder;

/// \brief QGroundControl state machine -- portable (QObject-based)
/// reimplementation selected when QGC_ENABLE_QT_STATEMACHINE=OFF.
///
/// See ../CMakeLists.txt's comment and tools/ghost/statemachine-port/
/// AUDIT.md for the full rationale (Qt6::StateMachine INTERFACE-links
/// Qt6::Gui/Qt6::DBus at the Qt package level -- wave-14 Q7d finding -- so
/// this is the only thing keeping those libraries in the headless ghost's
/// link graph).
///
/// This is a drop-in for the API surface AUDIT.md found actually used by
/// the 5 real consumer machines (InitialConnectStateMachine,
/// ComponentInformationManager, RequestMetaDataTypeStateMachine, and
/// ParameterManager's two ad-hoc PARAM_SET/PARAM_REQUEST_READ machines):
/// same class name, same method names/signatures for everything they call.
/// Framework surface AUDIT.md found unused by any consumer (parallel/
/// history/sub-machine states, event posting, guarded/self-loop/internal
/// transitions, StateContext access via context(), property-restore, dot
/// export, dead-end/unreachable-state analysis, ...) is omitted entirely
/// rather than stubbed -- nothing in the OFF build references it, and
/// AUDIT.md's own "what can be dropped" section explicitly sanctions this
/// for a v1 port.
class QGCStateMachine : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(QGCStateMachine)

    Q_PROPERTY(QString currentStateName READ currentStateName NOTIFY currentStateNameChanged)
    Q_PROPERTY(float progress READ progress NOTIFY progressUpdate)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QStringList stateHistory READ stateHistory NOTIFY stateHistoryChanged)

public:
    QGCStateMachine(const QString &machineName, Vehicle *vehicle, QObject *parent = nullptr);

    Vehicle *vehicle() const { return _vehicle; }
    QString machineName() const { return objectName(); }

    StateContext &context() { return _context; }
    const StateContext &context() const { return _context; }

    /// true if the state machine is currently running.
    bool isRunning() const { return _running; }
    /// Compatibility method matching old StateMachine::active().
    bool active() const { return isRunning(); }

    QString currentStateName() const;
    QStringList stateHistory() const { return _stateHistory; }
    void setStateHistoryLimit(int limit) { _stateHistoryLimit = limit; }

    /// Register a state with the machine and wire up global-error-state
    /// transitions if one is set. Every add*State() factory below calls
    /// this internally; consumers that construct states directly (`new
    /// XState(name, machine, ...)`) call it themselves too.
    void registerState(QGCAbstractState *state);

    FunctionState *addFunctionState(const QString &stateName, std::function<void()> function);
    AsyncFunctionState *addAsyncFunctionState(const QString &stateName,
                                               AsyncFunctionState::SetupFunction setupFunction,
                                               int timeoutMsecs = 0);
    QGCState *addErrorRecoveryState(const QString &stateName,
                                    ErrorRecoveryBuilder::Action action,
                                    int maxRetries = 0,
                                    int retryDelayMsecs = 1000,
                                    ErrorRecoveryBuilder::ExhaustedBehavior exhaustedBehavior = ErrorRecoveryBuilder::EmitError,
                                    ErrorRecoveryBuilder::Action fallback = nullptr,
                                    ErrorRecoveryBuilder::VoidAction rollback = nullptr,
                                    int timeoutMsecs = 0);

    /// Set the initial state and optionally start the machine.
    void setInitialState(QGCAbstractState *state, bool autoStart = false);

    /// Add a final state that will stop the machine when entered.
    QGCFinalState *addFinalState(const QString &stateName = QString());

    /// Set a global error state that all QGCState errors will transition to.
    /// AUDIT.md: never called by any of the 5 real consumers, so
    /// isInErrorState() below is always false in practice -- kept for API
    /// parity, not exercised.
    void setGlobalErrorState(QGCAbstractState *errorState) { _globalErrorState = errorState; }
    QGCAbstractState *globalErrorState() const { return _globalErrorState; }
    bool isInErrorState() const { return _globalErrorState && _currentState == _globalErrorState; }

    QGCAbstractState *findState(const QString &stateName) const { return findChild<QGCAbstractState *>(stateName); }

    /// Set up weighted progress tracking for a sequence of states.
    void setProgressWeights(const QList<QPair<QGCAbstractState *, int>> &stateWeights);
    /// Update sub-progress within the current state (0.0 to 1.0).
    void setSubProgress(float subProgress);
    float progress() const { return _progressLastEmitted; }
    void resetProgress();

    void setTimeoutOverride(const QString &stateName, int timeoutMsecs) { _timeoutOverrides[stateName] = timeoutMsecs; }
    void removeTimeoutOverride(const QString &stateName) { _timeoutOverrides.remove(stateName); }
    int timeoutOverride(const QString &stateName) const { return _timeoutOverrides.value(stateName, -1); }
    QHash<QString, int> allTimeoutOverrides() const { return _timeoutOverrides; }
    void clearTimeoutOverrides() { _timeoutOverrides.clear(); }
    QHash<QString, int> timeoutStats() const { return _timeoutStats; }
    void recordTimeout(const QString &stateName);
    void clearTimeoutStats() { _timeoutStats.clear(); }

    /// Create a retry transition that retries an action before advancing.
    /// @param from Source state (must match signal's object type)
    /// @param signal Signal that triggers retry evaluation (usually a timeout)
    /// @param to Target state (used after max retries)
    /// @param retryAction Action to perform on retry
    /// @param maxRetries Maximum number of retries before advancing
    template<typename SenderType, typename Func>
    RetryTransition *addRetryTransition(SenderType *from, Func signal,
                                        QGCAbstractState *to,
                                        std::function<void()> retryAction,
                                        int maxRetries = 1)
    {
        auto *fromState = qobject_cast<QGCAbstractState *>(from);
        return new RetryTransition(this, from, signal, fromState, to, std::move(retryAction), maxRetries);
    }

    void stop();

    /// Internal: posts a transition request onto the event loop, mirroring
    /// QStateMachine's own behavior of wrapping signal transitions in an
    /// internal event processed by the machine's event queue rather than
    /// executing them inline as part of the triggering emit(). See .cc for
    /// the full rationale (start()'s comment walks through the concrete
    /// nested-machine interleave this reproduces from
    /// tools/ghost/statemachine-port/baseline-mocklink-connect.log).
    /// Called by QGCAbstractState::addTransition()'s wiring and by
    /// RetryTransition -- not part of the consumer-facing API.
    void _queueTransition(QGCAbstractState *from, QGCAbstractState *to);

    // -------------------------------------------------------------------------
    // Diagnostics -- ported mechanically per AUDIT.md ("port as-is, no
    // redesign needed, just re-point at whatever the new base class emits").
    // -------------------------------------------------------------------------
    void setHistoryRecordingEnabled(bool enabled, int maxEntries = 1000);
    bool historyRecordingEnabled() const;
    QString dumpRecordedHistory() const;

    void setProfilingEnabled(bool enabled);
    bool profilingEnabled() const;
    QString profilingSummary() const;

    /// Enable/disable structured state-machine logger. This is the hook
    /// tools/ghost/statemachine-port/capture.sh's QGC_STATEMACHINE_LOG=1
    /// env var flips on -- its output format must stay byte-identical to
    /// the Qt-based original (see StateMachineLogger.cc).
    void setStructuredLoggingEnabled(bool enabled);
    bool structuredLoggingEnabled() const;

public slots:
    /// Start the state machine. Deliberately asynchronous -- see .cc.
    void start();

signals:
    void error();
    void progressUpdate(float progress);
    void currentStateNameChanged();
    void runningChanged();
    void stateHistoryChanged();

    void started();
    void stopped();
    void finished();

private:
    void _doStart();
    void _performTransition(QGCAbstractState *to);
    void _onStateEntered();
    float _calculateProgress() const;

    /// Posts (if not already pending) a single QueuedConnection dispatch of
    /// _drainSteps(), then appends 'step' to a *process-wide* pending queue
    /// shared by every QGCStateMachine instance (static, not per-instance --
    /// see the .cc for why: matching the Qt-based baseline's cross-machine
    /// transition interleave requires one global FIFO, the same way a
    /// single thread's QStateMachine instances all appear to share one
    /// underlying event-processing queue). Chained steps appended *during*
    /// _drainSteps() (e.g. a trivial FunctionState's immediate advance()
    /// re-triggering another transition) are drained in the SAME dispatch
    /// rather than each getting their own fresh trip through the Qt event
    /// queue. Used by start() and _queueTransition().
    static void _scheduleStep(std::function<void()> step);
    static void _drainSteps();

    Vehicle *_vehicle = nullptr;
    QGCAbstractState *_initialState = nullptr;
    QGCAbstractState *_currentState = nullptr;
    QGCAbstractState *_globalErrorState = nullptr;
    bool _running = false;

    // Progress tracking
    QList<QGCAbstractState *> _progressStates;
    QList<int> _progressWeights;
    int _progressTotalWeight = 0;
    int _progressCurrentIndex = -1;
    float _progressSubProgress = 0.0f;
    float _progressLastEmitted = 0.0f;

    // Inter-state data context
    StateContext _context;

    // QML state history
    QStringList _stateHistory;
    int _stateHistoryLimit = 20;

    // Timeout configuration
    QHash<QString, int> _timeoutOverrides;
    QHash<QString, int> _timeoutStats;

    // Optional runtime diagnostics helpers (lazy-created)
    StateHistoryRecorder *_historyRecorder = nullptr;
    StateMachineProfiler *_profiler = nullptr;
    StateMachineLogger *_logger = nullptr;
};
