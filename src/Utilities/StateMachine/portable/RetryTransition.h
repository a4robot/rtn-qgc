#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>

#include <functional>

class QGCStateMachine;
class QGCAbstractState;

Q_DECLARE_LOGGING_CATEGORY(RetryTransitionLog)

/// Portable counterpart of ../Transitions/RetryTransition.h. The Qt-based
/// original is a QSignalTransition subclass whose eventTest() blocks the
/// transition (returns false) while retries remain and allows it (returns
/// true) once exhausted; QStateMachine's own event-queue mechanics are what
/// give that "trigger fires repeatedly without actually transitioning"
/// behavior its timing. Here there is no QAbstractTransition object model,
/// so this is a plain QObject that connects directly to the triggering
/// signal and, once retries are exhausted, posts the transition through
/// QGCStateMachine::_queueTransition() -- the same queued path every other
/// addTransition()-wired transition uses, for the same event-loop-ordering
/// reasons (see QGCAbstractState::addTransition()'s comment).
class RetryTransition : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(RetryTransition)

public:
    using RetryAction = std::function<void()>;

    /// @param machine Owning state machine (used to post the eventual transition)
    /// @param sender Object emitting the trigger signal (usually the source state)
    /// @param signal Signal that triggers retry/advance evaluation (usually a timeout)
    /// @param source Source state (for restartWait()/logging; usually == sender)
    /// @param target State to transition to after max retries exhausted
    /// @param retryAction Action to invoke on each retry attempt
    /// @param maxRetries Maximum retry attempts before transitioning (default: 1)
    template<typename SenderType, typename Func>
    RetryTransition(QGCStateMachine *machine, SenderType *sender, Func signal,
                     QGCAbstractState *source, QGCAbstractState *target,
                     RetryAction retryAction, int maxRetries = 1)
        : QObject(machine)
        , _machine(machine)
        , _source(source)
        , _target(target)
        , _retryAction(std::move(retryAction))
        , _maxRetries(maxRetries)
    {
        QObject::connect(sender, signal, this, &RetryTransition::_onTriggered);
    }

    /// Reset retry count (call when re-entering the source state)
    void reset() { _retryCount = 0; }

    int retryCount() const { return _retryCount; }
    int maxRetries() const { return _maxRetries; }

private slots:
    void _onTriggered();

private:
    QGCStateMachine *_machine = nullptr;
    QGCAbstractState *_source = nullptr;
    QGCAbstractState *_target = nullptr;
    RetryAction _retryAction;
    int _maxRetries = 1;
    int _retryCount = 0;
};
