#include "WaitStateBase.h"
#include "QGCStateMachine.h"

WaitStateBase::WaitStateBase(const QString &stateName, QGCStateMachine *machine, int timeoutMsecs)
    : QGCState(stateName, machine)
    , _timeoutMsecs(timeoutMsecs > 0 ? timeoutMsecs : 0)
{
    _timeoutTimer.setSingleShot(true);
    connect(&_timeoutTimer, &QTimer::timeout, this, &WaitStateBase::_onTimeout);
    connect(this, &QGCAbstractState::entered, this, &WaitStateBase::_onEntered);
    connect(this, &QGCAbstractState::exited, this, &WaitStateBase::_onExited);
}

void WaitStateBase::_onEntered()
{
    _completed = false;
    connectWaitSignal();
    onWaitEntered();

    // Check for runtime timeout override
    int effectiveTimeout = _timeoutMsecs;
    if (machine()) {
        int override = machine()->timeoutOverride(objectName());
        if (override >= 0) {
            effectiveTimeout = override;
            qCDebug(QGCStateMachineLog) << stateName() << "using timeout override:" << effectiveTimeout << "ms";
        }
    }

    if (effectiveTimeout > 0) {
        _timeoutTimer.start(effectiveTimeout);
    }
}

void WaitStateBase::_onExited()
{
    _timeoutTimer.stop();
    disconnectWaitSignal();
    onWaitExited();
}

void WaitStateBase::_onTimeout()
{
    if (_completed) {
        return;
    }

    qCDebug(QGCStateMachineLog) << "Timeout" << stateName();

    if (machine()) {
        machine()->recordTimeout(objectName());
    }

    disconnectWaitSignal();
    onWaitTimeout();
}

void WaitStateBase::onWaitEntered()
{
    // Default implementation does nothing - subclasses can override
}

void WaitStateBase::onWaitExited()
{
    // Default implementation does nothing - subclasses can override
}

void WaitStateBase::onWaitTimeout()
{
    emit timeout();
    emit timedOut();
}

void WaitStateBase::waitComplete()
{
    if (_completed) {
        return;
    }
    _completed = true;

    _timeoutTimer.stop();
    disconnectWaitSignal();

    emit completed();
    emit advance();
}

void WaitStateBase::waitFailed()
{
    if (_completed) {
        return;
    }
    _completed = true;

    _timeoutTimer.stop();
    disconnectWaitSignal();

    emit error();
}

void WaitStateBase::restartWait()
{
    if (_completed) {
        return;
    }

    disconnectWaitSignal();
    connectWaitSignal();

    int effectiveTimeout = _timeoutMsecs;
    if (machine()) {
        const int override = machine()->timeoutOverride(objectName());
        if (override >= 0) {
            effectiveTimeout = override;
        }
    }

    if (effectiveTimeout > 0) {
        _timeoutTimer.start(effectiveTimeout);
    }
}
