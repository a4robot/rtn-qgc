#include "RetryTransition.h"
#include "QGCStateMachine.h"
#include "QGCState.h"
#include "WaitStateBase.h"

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(RetryTransitionLog, "Utilities.StateMachine.RetryTransition")

void RetryTransition::_onTriggered()
{
    const QString name = _source ? _source->stateName() : QStringLiteral("unknown");

    if (_retryCount < _maxRetries) {
        _retryCount++;
        qCDebug(RetryTransitionLog) << name << "timeout, retry" << _retryCount << "of" << _maxRetries;

        if (auto *waitState = qobject_cast<WaitStateBase *>(_source)) {
            waitState->restartWait();
        }

        if (_retryAction) {
            _retryAction();
        }

        // Blocked -- stay in the current state, no transition.
        return;
    }

    qCWarning(RetryTransitionLog) << name << "timeout after" << _maxRetries << "retries, advancing";
    _retryCount = 0;

    if (_machine && _source) {
        _machine->_queueTransition(_source, _target);
    }
}
