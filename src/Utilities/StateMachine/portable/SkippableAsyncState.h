#pragma once

#include "WaitStateBase.h"

#include <functional>

/// Portable counterpart of ../States/SkippableAsyncState.h (unchanged
/// behavior; see WaitStateBase.h).
class SkippableAsyncState : public WaitStateBase
{
    Q_OBJECT
    Q_DISABLE_COPY(SkippableAsyncState)

public:
    using SkipPredicate = std::function<bool()>;
    using SetupFunction = std::function<void(SkippableAsyncState *state)>;
    using SkipAction = std::function<void()>;

    SkippableAsyncState(const QString &stateName,
                        QGCStateMachine *machine,
                        SkipPredicate skipPredicate,
                        SetupFunction setupFunc,
                        SkipAction skipAction = nullptr,
                        int timeoutMsecs = 0);

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

    void setSkipPredicate(SkipPredicate predicate) { _skipPredicate = std::move(predicate); }
    void setSetupFunction(SetupFunction setupFunc) { _setupFunction = std::move(setupFunc); }
    void setSkipAction(SkipAction skipAction) { _skipAction = std::move(skipAction); }

signals:
    void skipped();

protected:
    void connectWaitSignal() override;
    void disconnectWaitSignal() override;
    void onWaitEntered() override;

private:
    SkipPredicate _skipPredicate;
    SetupFunction _setupFunction;
    SkipAction _skipAction;
    QMetaObject::Connection _completionConnection;
    bool _wasSkipped = false;
};
