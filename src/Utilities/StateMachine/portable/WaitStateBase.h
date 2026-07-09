#pragma once

#include "QGCState.h"

#include <QtCore/QTimer>

/// Portable counterpart of ../States/WaitStateBase.h. Behavior is
/// unchanged from the Qt-based original (AUDIT.md: "this class needs no
/// behavioral change, only its QState/QGCState base needs replacing") --
/// it already implements per-state timeout with a plain QTimer, not Qt's
/// state-machine TimeoutTransition machinery.
class WaitStateBase : public QGCState
{
    Q_OBJECT
    Q_DISABLE_COPY(WaitStateBase)

public:
    /// @param stateName Name for this state (for logging)
    /// @param machine Owning state machine
    /// @param timeoutMsecs Timeout in milliseconds, 0 for no timeout
    WaitStateBase(const QString &stateName, QGCStateMachine *machine, int timeoutMsecs = 0);

    /// Rearm wait signal connections and timeout after a handled timeout event.
    /// Intended for retry loops that stay in the same state.
    void restartWait();

signals:
    /// Emitted when the wait condition is satisfied (alias for advance())
    void completed();

    /// Emitted when the timeout expires before the wait condition is met
    void timeout();

    /// Emitted when the timeout expires (alias for timeout())
    void timedOut();

protected:
    virtual void onWaitEntered();
    virtual void onWaitExited();
    virtual void onWaitTimeout();

    /// Call this when the wait condition is satisfied.
    void waitComplete();

    /// Call this when the wait fails.
    void waitFailed();

    virtual void connectWaitSignal() = 0;
    virtual void disconnectWaitSignal() = 0;

    int timeoutMsecs() const { return _timeoutMsecs; }

private slots:
    void _onEntered();
    void _onExited();
    void _onTimeout();

private:
    int _timeoutMsecs = 0;
    QTimer _timeoutTimer;
    bool _completed = false;
};
