#include "FunctionState.h"

/// Executes a function when the state is entered
FunctionState::FunctionState(const QString &stateName, QGCStateMachine *machine, std::function<void()> function)
    : QGCState(stateName, machine)
    , _function(function)
{
    connect(this, &QGCAbstractState::entered, this, [this]() {
        _function();
        emit advance();
    });
}
