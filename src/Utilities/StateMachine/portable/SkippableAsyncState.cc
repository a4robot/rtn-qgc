#include "SkippableAsyncState.h"

#include <QtCore/QMetaObject>

SkippableAsyncState::SkippableAsyncState(const QString &stateName,
                                         QGCStateMachine *machine,
                                         SkipPredicate skipPredicate,
                                         SetupFunction setupFunc,
                                         SkipAction skipAction,
                                         int timeoutMsecs)
    : WaitStateBase(stateName, machine, timeoutMsecs)
    , _skipPredicate(std::move(skipPredicate))
    , _setupFunction(std::move(setupFunc))
    , _skipAction(std::move(skipAction))
{
}

void SkippableAsyncState::connectWaitSignal()
{
    // Connection is set up dynamically via connectToCompletion() in the setup function
}

void SkippableAsyncState::disconnectWaitSignal()
{
    if (_completionConnection) {
        disconnect(_completionConnection);
        _completionConnection = {};
    }
}

void SkippableAsyncState::onWaitEntered()
{
    _wasSkipped = false;

    if (_skipPredicate && _skipPredicate()) {
        qCDebug(QGCStateMachineLog) << "Skip condition met, skipping" << stateName();
        _wasSkipped = true;

        if (_skipAction) {
            _skipAction();
        }

        // Defer signal emission to avoid firing during state entry -- this
        // ensures the state machine finishes entering before transitions
        // are evaluated. Already deliberately queued in the Qt-based
        // original for the same reason; preserved here unchanged.
        QMetaObject::invokeMethod(this, &SkippableAsyncState::skipped, Qt::QueuedConnection);
        return;
    }

    qCDebug(QGCStateMachineLog) << "Condition not met, executing" << stateName();
    if (_setupFunction) {
        _setupFunction(this);
    }
}
