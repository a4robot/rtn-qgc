#pragma once

#include "QGCState.h"

#include <functional>

/// Portable counterpart of ../States/FunctionState.h.
class FunctionState : public QGCState
{
    Q_OBJECT
    Q_DISABLE_COPY(FunctionState)

public:
    FunctionState(const QString &stateName, QGCStateMachine *machine, std::function<void()> function);

private:
    std::function<void()> _function;
};
