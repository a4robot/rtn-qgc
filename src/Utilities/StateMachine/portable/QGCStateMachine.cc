#include "QGCStateMachine.h"
#include "StateHistoryRecorder.h"
#include "StateMachineLogger.h"
#include "StateMachineProfiler.h"
#include "Vehicle.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QMetaObject>
#include <QtCore/QPointer>

namespace {
/// Process-wide, shared by every QGCStateMachine instance -- see
/// QGCStateMachine::_scheduleStep()'s header comment. A step queued by one
/// machine (e.g. a nested/reused machine's start(), or a callback that
/// completes a *different* machine's state -- ComponentInformationManager's
/// "Complete" state finishing InitialConnectStateMachine's "RequestCompInfo"
/// wait, for one concrete example this was reverse-engineered against) must
/// interleave FIFO with every other machine's pending steps, not just this
/// instance's own -- a per-instance queue would let one machine's whole
/// chain drain before any cross-machine-triggered step gets a turn, which
/// does not match tools/ghost/statemachine-port/baseline-mocklink-connect.log
/// (see that file's ComponentInformationManager "Complete"/"Final" vs
/// InitialConnectStateMachine "RequestCompInfo" exit / "RequestParameters"
/// entry ordering).
QList<std::function<void()>> &qgcStateMachinePendingSteps()
{
    static QList<std::function<void()>> steps;
    return steps;
}
} // namespace

QGCStateMachine::QGCStateMachine(const QString &machineName, Vehicle *vehicle, QObject *parent)
    : QObject(parent)
    , _vehicle(vehicle)
{
    setObjectName(machineName);

    // Connection order matters here: this lambda must be connected to
    // started()/stopped()/finished() *before* setStructuredLoggingEnabled()
    // (below) creates a StateMachineLogger and connects its own slots to
    // the same signals, so the plain "State machine started:"/"...stopped:"/
    // "...finished:" qCDebug lines always appear before StateMachineLogger's
    // own "▷ Machine started: ..." lines -- matching
    // tools/ghost/statemachine-port/baseline-mocklink-connect.log (e.g.
    // lines 12-13: "State machine started: ..." then "▷ Machine started: ...").
    connect(this, &QGCStateMachine::started, this, [this]() {
        qCDebug(QGCStateMachineLog) << "State machine started:" << objectName();
        emit runningChanged();
    });
    connect(this, &QGCStateMachine::stopped, this, [this]() {
        qCDebug(QGCStateMachineLog) << "State machine stopped:" << objectName();
        emit runningChanged();
    });
    connect(this, &QGCStateMachine::finished, this, [this]() {
        qCDebug(QGCStateMachineLog) << "State machine finished:" << objectName();
        if (_progressTotalWeight > 0 && _progressLastEmitted < 1.0f) {
            _progressLastEmitted = 1.0f;
            emit progressUpdate(1.0f);
        }
    });

    if (qEnvironmentVariableIsSet("QGC_STATEMACHINE_HISTORY")) {
        setHistoryRecordingEnabled(true);
    }
    if (qEnvironmentVariableIsSet("QGC_STATEMACHINE_PROFILE")) {
        setProfilingEnabled(true);
    }
    if (qEnvironmentVariableIsSet("QGC_STATEMACHINE_LOG")) {
        setStructuredLoggingEnabled(true);
    }
}

QString QGCStateMachine::currentStateName() const
{
    return _currentState ? _currentState->objectName() : QString();
}

void QGCStateMachine::registerState(QGCAbstractState *state)
{
    if (_globalErrorState) {
        state->addTransition(state, &QGCAbstractState::error, _globalErrorState);
    }
}

FunctionState *QGCStateMachine::addFunctionState(const QString &stateName, std::function<void()> function)
{
    auto *state = new FunctionState(stateName, this, std::move(function));
    registerState(state);
    return state;
}

AsyncFunctionState *QGCStateMachine::addAsyncFunctionState(const QString &stateName,
                                                            AsyncFunctionState::SetupFunction setupFunction,
                                                            int timeoutMsecs)
{
    auto *state = new AsyncFunctionState(stateName, this, std::move(setupFunction), timeoutMsecs);
    registerState(state);
    return state;
}

QGCState *QGCStateMachine::addErrorRecoveryState(const QString &stateName,
                                                 ErrorRecoveryBuilder::Action action,
                                                 int maxRetries,
                                                 int retryDelayMsecs,
                                                 ErrorRecoveryBuilder::ExhaustedBehavior exhaustedBehavior,
                                                 ErrorRecoveryBuilder::Action fallback,
                                                 ErrorRecoveryBuilder::VoidAction rollback,
                                                 int timeoutMsecs)
{
    ErrorRecoveryBuilder builder(this, stateName);
    builder.withAction(std::move(action))
           .retry(maxRetries, retryDelayMsecs)
           .onExhausted(exhaustedBehavior);

    if (fallback) {
        builder.withFallback(std::move(fallback));
    }
    if (rollback) {
        builder.withRollback(std::move(rollback));
    }
    if (timeoutMsecs > 0) {
        builder.withTimeout(timeoutMsecs);
    }

    return builder.build();
}

void QGCStateMachine::setInitialState(QGCAbstractState *state, bool autoStart)
{
    _initialState = state;
    if (autoStart) {
        start();
    }
}

QGCFinalState *QGCStateMachine::addFinalState(const QString &stateName)
{
    return new QGCFinalState(stateName.isEmpty() ? QStringLiteral("Final") : stateName, this);
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

void QGCStateMachine::start()
{
    if (_running) {
        qCCritical(QGCStateMachineLog) << objectName() << "start() called but already running - check signal connections";
    }

    // Deliberately posted (Qt::QueuedConnection via _scheduleStep()), not
    // entered synchronously. This mirrors QStateMachine::start(), which the
    // Qt docs describe as asynchronous: signal-triggered transitions (and
    // the initial start itself) are processed through the machine's event
    // queue, not inline with the call that triggered them.
    //
    // Concretely, this is what makes nested/reused machines interleave in
    // the same order as tools/ghost/statemachine-port/baseline-
    // mocklink-connect.log. Example (lines 21-30): ComponentInformation
    // Manager::requestAllComponentInformation() calls start() (this
    // function), which schedules _doStart(). Once dequeued, _doStart()
    // enters CIM's initial state synchronously; that state's setup function
    // synchronously calls RequestMetaDataTypeStateMachine::request(), which
    // itself calls start() on a *different* QGCStateMachine instance --
    // scheduling a step on *that* machine's own (currently empty) pending
    // queue, which therefore gets its own fresh QueuedConnection dispatch,
    // landing after CIM's current dispatch finishes. CIM's own _doStart()
    // then finishes (still the same dispatch) by emitting started(),
    // logging "State machine started: ComponentInformationManager". Only
    // *after* that does the event loop reach the nested machine's queued
    // dispatch, which is when "Entered RequestMetaDataType:
    // RequestCompMetadata" appears. Making start() synchronous instead
    // collapses this into the wrong order.
    QPointer<QGCStateMachine> self(this);
    _scheduleStep([self]() {
        if (self) {
            self->_doStart();
        }
    });
}

void QGCStateMachine::_scheduleStep(std::function<void()> step)
{
    auto &steps = qgcStateMachinePendingSteps();
    const bool wasEmpty = steps.isEmpty();
    steps.append(std::move(step));

    if (wasEmpty) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), []() {
            QGCStateMachine::_drainSteps();
        }, Qt::QueuedConnection);
    }
    // If the queue was already non-empty, a dispatch is already posted (or
    // _drainSteps() is currently executing and will pick this step up
    // itself -- see below) -- no second post needed.
}

void QGCStateMachine::_drainSteps()
{
    // Drains every step queued so far, across every QGCStateMachine
    // instance, INCLUDING ones appended as a side effect of running an
    // earlier step in this same call (e.g. a trivial FunctionState's
    // immediate advance() synchronously re-triggering _queueTransition()
    // for the next state, possibly on a *different* machine). This mirrors
    // QStateMachine's own internal event processing, which drains queued
    // work in a tight loop rather than yielding back to the outer Qt event
    // loop between each chained transition.
    //
    // This matters beyond cosmetics: an earlier revision of this port
    // posted one fresh QueuedConnection per transition, which gave *other*
    // main-thread work a fresh opportunity to interleave ahead of every
    // single hop. For flat chains of trivial states (e.g. ParameterManager's
    // PARAM_SET machine: SendMavlinkMessageState -> a FunctionState ->
    // WaitForParamResponseState, all of which complete synchronously except
    // the last), that meant the wait-state's response listener could be
    // armed measurably later than under the Qt-based original -- which
    // processes exactly this kind of chain in one tight internal pass --
    // making the listener lose more of its race against MockLink's
    // worker-thread response than it should (observed: the ad-hoc PARAM_SET
    // machine's WaitForParamResponseState spuriously timing out against
    // MockLink's *own* worker thread in a large fraction of runs). Draining
    // here instead of re-posting per hop closes that gap: only the *first*
    // step in a chain (the one that found the queue empty) ever waits on a
    // fresh dispatch; every synchronously-chained follow-up runs
    // immediately -- whichever machine it belongs to.
    auto &steps = qgcStateMachinePendingSteps();
    while (!steps.isEmpty()) {
        auto step = steps.takeFirst();
        step();
    }
}

void QGCStateMachine::_doStart()
{
    _running = true;

    if (_initialState) {
        _currentState = _initialState;
        emit currentStateNameChanged();
        // Synchronous: entering a state runs its full onEntry chain
        // (including e.g. AsyncFunctionState's setup function) inline, just
        // like the Qt-based original's onEntry(QEvent*) override chain.
        // Only *new* transitions/starts triggered from within that chain
        // get freshly queued (see addTransition()'s and start()'s comments).
        _currentState->enterState();
    }

    emit started();

    if (_currentState && _currentState->isFinalState()) {
        // Edge case (initial state is itself a final state) -- not
        // exercised by any of the 5 real consumer machines, kept for
        // correctness/API completeness.
        _running = false;
        emit finished();
    }
}

void QGCStateMachine::stop()
{
    if (!_running) {
        return;
    }
    _running = false;
    emit stopped();
}

// -----------------------------------------------------------------------------
// Transitions
// -----------------------------------------------------------------------------

void QGCStateMachine::_queueTransition(QGCAbstractState *from, QGCAbstractState *to)
{
    QPointer<QGCStateMachine> self(this);
    QPointer<QGCAbstractState> fromPtr(from);
    _scheduleStep([self, fromPtr, to]() {
        if (!self) {
            return;
        }
        if (self->_currentState != fromPtr) {
            // Stale: 'from' is no longer the active state -- a different,
            // earlier-queued transition already moved the machine on.
            // Mirrors QStateMachine only taking a transition while its
            // source state is in the current configuration.
            return;
        }
        self->_performTransition(to);
    });
}

void QGCStateMachine::_performTransition(QGCAbstractState *to)
{
    if (_currentState) {
        _currentState->exitState();
    }

    _currentState = to;
    emit currentStateNameChanged();

    if (to) {
        to->enterState();

        if (to->isFinalState()) {
            _running = false;
            emit finished();
        }
    }
}

// -----------------------------------------------------------------------------
// Progress Tracking
// -----------------------------------------------------------------------------

void QGCStateMachine::setProgressWeights(const QList<QPair<QGCAbstractState *, int>> &stateWeights)
{
    for (auto *state : std::as_const(_progressStates)) {
        disconnect(state, &QGCAbstractState::entered, this, &QGCStateMachine::_onStateEntered);
    }

    _progressStates.clear();
    _progressWeights.clear();
    _progressTotalWeight = 0;

    for (const auto &pair : stateWeights) {
        _progressStates.append(pair.first);
        _progressWeights.append(pair.second);
        _progressTotalWeight += pair.second;

        connect(pair.first, &QGCAbstractState::entered, this, &QGCStateMachine::_onStateEntered);
    }

    qCDebug(QGCStateMachineLog) << objectName() << "progress tracking enabled for"
                                 << _progressStates.size() << "states, total weight:" << _progressTotalWeight;
}

void QGCStateMachine::_onStateEntered()
{
    auto *state = qobject_cast<QGCAbstractState *>(sender());
    if (!state) return;

    QString stateNameStr = state->objectName();
    if (!stateNameStr.isEmpty()) {
        _stateHistory.append(stateNameStr);
        while (_stateHistory.size() > _stateHistoryLimit) {
            _stateHistory.removeFirst();
        }
        emit stateHistoryChanged();
        emit currentStateNameChanged();
    }

    int index = _progressStates.indexOf(state);
    if (index >= 0 && index != _progressCurrentIndex) {
        _progressCurrentIndex = index;
        _progressSubProgress = 0.0f;

        float newProgress = _calculateProgress();
        if (newProgress > _progressLastEmitted) {
            _progressLastEmitted = newProgress;
            emit progressUpdate(newProgress);
        }
    }
}

void QGCStateMachine::setSubProgress(float subProgress)
{
    _progressSubProgress = qBound(0.0f, subProgress, 1.0f);

    float newProgress = _calculateProgress();
    if (newProgress > _progressLastEmitted) {
        _progressLastEmitted = newProgress;
        emit progressUpdate(newProgress);
    }
}

void QGCStateMachine::resetProgress()
{
    _progressCurrentIndex = -1;
    _progressSubProgress = 0.0f;
    _progressLastEmitted = 0.0f;
}

float QGCStateMachine::_calculateProgress() const
{
    if (_progressTotalWeight <= 0 || _progressCurrentIndex < 0) {
        return 0.0f;
    }

    int completedWeight = 0;
    for (int i = 0; i < _progressCurrentIndex && i < _progressWeights.size(); ++i) {
        completedWeight += _progressWeights[i];
    }

    int currentWeight = (_progressCurrentIndex < _progressWeights.size())
                            ? _progressWeights[_progressCurrentIndex]
                            : 1;

    return (completedWeight + currentWeight * _progressSubProgress) / static_cast<float>(_progressTotalWeight);
}

// -----------------------------------------------------------------------------
// Timeout Configuration
// -----------------------------------------------------------------------------

void QGCStateMachine::recordTimeout(const QString &stateName)
{
    _timeoutStats[stateName]++;
    qCDebug(QGCStateMachineLog) << objectName() << "timeout recorded for" << stateName
                                 << "total:" << _timeoutStats[stateName];
}

// -----------------------------------------------------------------------------
// Diagnostics
// -----------------------------------------------------------------------------

void QGCStateMachine::setHistoryRecordingEnabled(bool enabled, int maxEntries)
{
    if (!_historyRecorder) {
        _historyRecorder = new StateHistoryRecorder(this, maxEntries);
    }
    _historyRecorder->setMaxEntries(maxEntries);
    _historyRecorder->setEnabled(enabled);
}

bool QGCStateMachine::historyRecordingEnabled() const
{
    return _historyRecorder && _historyRecorder->isEnabled();
}

QString QGCStateMachine::dumpRecordedHistory() const
{
    return _historyRecorder ? _historyRecorder->dumpHistory() : QString();
}

void QGCStateMachine::setProfilingEnabled(bool enabled)
{
    if (!_profiler) {
        _profiler = new StateMachineProfiler(this);
    }
    _profiler->setEnabled(enabled);
}

bool QGCStateMachine::profilingEnabled() const
{
    return _profiler && _profiler->isEnabled();
}

QString QGCStateMachine::profilingSummary() const
{
    return _profiler ? _profiler->summary() : QString();
}

void QGCStateMachine::setStructuredLoggingEnabled(bool enabled)
{
    if (!_logger) {
        _logger = new StateMachineLogger(this, this);
    }
    _logger->setEnabled(enabled);
}

bool QGCStateMachine::structuredLoggingEnabled() const
{
    return _logger && _logger->isEnabled();
}
