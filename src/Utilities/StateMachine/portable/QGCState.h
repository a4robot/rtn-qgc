#pragma once

#include "QGCAbstractState.h"

/// Portable counterpart of ../States/QGCState.h. Adds addThisTransition()
/// convenience over QGCAbstractState::addTransition(). The Qt-based
/// original also offers local-error-state wiring (setLocalErrorState()) and
/// QState::assignProperty()-based property restore; AUDIT.md's feature
/// matrix confirms neither is used by any of the 5 real consumer machines
/// (setGlobalErrorState() is never called either, so the local-vs-global
/// distinction that mechanism exists for is moot), so this port omits both
/// rather than carrying dead surface -- see AUDIT.md's "what can be
/// dropped, not reimplemented" section.
class QGCState : public QGCAbstractState
{
    Q_OBJECT
    Q_DISABLE_COPY(QGCState)

public:
    QGCState(const QString &stateName, QGCStateMachine *machine);

    /// Simpler version of addTransition() which assumes the sender is this.
    template<typename PointerToMemberFunction>
    void addThisTransition(PointerToMemberFunction signal, QGCAbstractState *target)
    {
        addTransition(this, signal, target);
    }
};
