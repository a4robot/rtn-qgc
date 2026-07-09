#include "QGCAbstractState.h"
#include "QGCStateMachine.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(QGCStateMachineLog, "Utilities.QGCStateMachine")

QGCAbstractState::QGCAbstractState(const QString &stateName, QGCStateMachine *machine)
    : QObject(machine)
    , _machine(machine)
{
    setObjectName(stateName);
}

Vehicle *QGCAbstractState::vehicle() const
{
    return _machine ? _machine->vehicle() : nullptr;
}

QString QGCAbstractState::stateName() const
{
    return _machine ? QStringLiteral("%1:%2").arg(_machine->machineName(), objectName()) : objectName();
}

StateContext *QGCAbstractState::context() const
{
    return _machine ? &_machine->context() : nullptr;
}

void QGCAbstractState::setCallbacks(EntryCallback onEntry, ExitCallback onExit)
{
    _entryCallback = std::move(onEntry);
    _exitCallback = std::move(onExit);
}

void QGCAbstractState::enterState()
{
    qCDebug(QGCStateMachineLog) << "Entered" << stateName();

    if (_entryCallback) {
        _entryCallback();
    }

    onEnter();

    emit entered();
}

void QGCAbstractState::exitState()
{
    onLeave();

    if (_exitCallback) {
        _exitCallback();
    }

    qCDebug(QGCStateMachineLog) << "Exited" << stateName();

    emit exited();
}

void QGCAbstractState::_requestTransition(QGCAbstractState *target)
{
    if (_machine) {
        _machine->_queueTransition(this, target);
    }
}

bool QGCAbstractState::event(QEvent *event)
{
    if (_eventHandler && _eventHandler(event)) {
        return true;
    }

    return QObject::event(event);
}
