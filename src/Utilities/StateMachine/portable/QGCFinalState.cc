#include "QGCFinalState.h"
#include "QGCStateMachine.h"

QGCFinalState::QGCFinalState(const QString &stateName, QGCStateMachine *machine)
    : QGCAbstractState(stateName, machine)
{
}

QGCFinalState::QGCFinalState(QGCStateMachine *machine)
    : QGCFinalState(QStringLiteral("FinalState"), machine)
{
}

QString QGCFinalState::stateName() const
{
    // Deliberately reversed vs QGCAbstractState::stateName() ("ObjectName:
    // MachineName" instead of "MachineName:ObjectName") -- matches the
    // Qt-based QGCFinalState's own (separately implemented) format exactly.
    // See tools/ghost/statemachine-port/baseline-mocklink-connect.log lines
    // like `Entered "Final:RequestMetaDataType"` (objectName "Final",
    // machine "RequestMetaDataType").
    return machine() ? QStringLiteral("%1:%2").arg(objectName(), machine()->machineName()) : objectName();
}
