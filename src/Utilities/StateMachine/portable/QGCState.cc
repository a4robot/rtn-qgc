#include "QGCState.h"

QGCState::QGCState(const QString &stateName, QGCStateMachine *machine)
    : QGCAbstractState(stateName, machine)
{
}
