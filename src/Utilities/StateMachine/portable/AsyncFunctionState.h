#pragma once

#include "WaitStateBase.h"

#include <functional>

/// Portable counterpart of ../States/AsyncFunctionState.h (unchanged
/// behavior; see WaitStateBase.h).
class AsyncFunctionState : public WaitStateBase
{
    Q_OBJECT
    Q_DISABLE_COPY(AsyncFunctionState)

public:
    using SetupFunction = std::function<void(AsyncFunctionState *state)>;

    AsyncFunctionState(const QString &stateName, QGCStateMachine *machine, SetupFunction setupFunction, int timeoutMsecs = 0);

    void complete() { waitComplete(); }
    void fail() { waitFailed(); }

    template<typename Func>
    void connectToCompletion(typename QtPrivate::FunctionPointer<Func>::Object *sender, Func signal)
    {
        _completionConnection = connect(sender, signal, this, [this]() { complete(); });
    }

    template<typename Func, typename Slot>
    void connectToCompletion(typename QtPrivate::FunctionPointer<Func>::Object *sender, Func signal, Slot slot)
    {
        _completionConnection = connect(sender, signal, this, slot);
    }

protected:
    void connectWaitSignal() override;
    void disconnectWaitSignal() override;
    void onWaitEntered() override;

private:
    SetupFunction _setupFunction;
    QMetaObject::Connection _completionConnection;
};
