#pragma once

#include "QGCAbstractState.h"

/// Portable counterpart of ../States/QGCFinalState.h. In the Qt-based
/// original this derives from QFinalState (a sibling of QState, not
/// QGCAbstractState), so it has its own separately-implemented
/// "Entered"/"Exited" logging wired directly to the entered/exited signals
/// in its constructor. Here it derives from QGCAbstractState instead (the
/// common base every transition target needs in this port) and overrides
/// stateName() so QGCAbstractState::enterState()/exitState()'s existing
/// logging produces identical text through virtual dispatch -- no
/// duplicated log lines needed.
class QGCFinalState : public QGCAbstractState
{
    Q_OBJECT
    Q_DISABLE_COPY(QGCFinalState)

public:
    explicit QGCFinalState(const QString &stateName, QGCStateMachine *machine);
    explicit QGCFinalState(QGCStateMachine *machine);

    QString stateName() const override;
    bool isFinalState() const override { return true; }
};
