#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>

#include <functional>

class QGCStateMachine;
class Vehicle;
class StateContext;

Q_DECLARE_LOGGING_CATEGORY(QGCStateMachineLog)

/// \brief Lightweight base class for simple states -- portable (QObject-based)
/// reimplementation selected when QGC_ENABLE_QT_STATEMACHINE=OFF.
///
/// See ../CMakeLists.txt's comment and tools/ghost/statemachine-port/AUDIT.md
/// for why this directory exists: the Qt-based QGCAbstractState
/// (../States/QGCAbstractState.h) derives from QState, which INTERFACE-links
/// Qt6::Gui via the Qt6::StateMachine Qt package (wave-14 Q7d finding). This
/// class preserves the exact public API/semantics the 5 real consumer
/// machines use (AUDIT.md's feature-surface matrix) on top of plain QObject
/// + explicit machine-driven bookkeeping instead of QState/QStateMachine.
///
/// Unlike the Qt-based original, this models exactly what every real
/// consumer does: a flat state that is always a direct, permanent child of
/// one QGCStateMachine (every State subclass constructor takes the owning
/// machine directly, never another state -- AUDIT.md confirms all 5 machines
/// are flat, single-level, with no nested/parallel states anywhere).
class QGCAbstractState : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(QGCAbstractState)

public:
    using EntryCallback = std::function<void()>;
    using ExitCallback = std::function<void()>;
    using EventHandler = std::function<bool(QEvent*)>;

    QGCAbstractState(const QString &stateName, QGCStateMachine *machine);

    QGCStateMachine *machine() const { return _machine; }
    Vehicle *vehicle() const;

    /// "MachineName:StateName" -- matches the Qt-based QGCAbstractState::
    /// stateName() format exactly (compared verbatim in replay-compares
    /// against tools/ghost/statemachine-port/baseline-mocklink-connect.log).
    /// Virtual so QGCFinalState (which uses the reversed "StateName:
    /// MachineName" format the Qt-based QGCFinalState always used) can
    /// override it while reusing enterState()/exitState()'s logging.
    virtual QString stateName() const;

    StateContext *context() const;

    void setOnEntry(EntryCallback callback) { _entryCallback = std::move(callback); }
    void setOnExit(ExitCallback callback) { _exitCallback = std::move(callback); }
    void setCallbacks(EntryCallback onEntry, ExitCallback onExit = nullptr);
    void setEventHandler(EventHandler handler) { _eventHandler = std::move(handler); }

    /// true only for QGCFinalState. Checked by QGCStateMachine after a
    /// transition completes to decide whether to stop the machine and emit
    /// finished() -- the QObject-based stand-in for reaching a top-level
    /// QFinalState under real QStateMachine.
    virtual bool isFinalState() const { return false; }

    /// Mirrors QState::addTransition(sender, signal, target) and
    /// QGCState::addThisTransition(signal, target) from the Qt-based
    /// framework: wires 'signal' (emitted by 'sender') to request a
    /// transition from this state to 'target'.
    ///
    /// IMPORTANT: the transition is *not* performed synchronously inside the
    /// signal emission. It is posted onto this state's machine via
    /// QGCStateMachine::_queueTransition(), mirroring QStateMachine's own
    /// behavior: signal transitions are wrapped in an internal event and
    /// processed on the machine's event queue, not executed inline as part
    /// of emit(). Making this synchronous instead would collapse nested/nearly
    /// -simultaneous transitions into a different order than
    /// tools/ghost/statemachine-port/baseline-mocklink-connect.log records
    /// (see QGCStateMachine.cc's start()/​_queueTransition() comments for the
    /// concrete example this was reverse-engineered from).
    template<typename Sender, typename Func>
    void addTransition(Sender *sender, Func signal, QGCAbstractState *target)
    {
        QObject::connect(sender, signal, this, [this, target]() { _requestTransition(target); });
    }

    /// Called by QGCStateMachine when this state becomes (or stops being)
    /// the machine's current state. Not part of the consumer-facing API.
    void enterState();
    void exitState();

signals:
    void entered();
    void exited();
    void advance();
    void error();

protected:
    virtual void onEnter() {}
    virtual void onLeave() {}
    bool event(QEvent *event) override;

private:
    void _requestTransition(QGCAbstractState *target);

    QGCStateMachine *_machine = nullptr;
    EntryCallback _entryCallback;
    ExitCallback _exitCallback;
    EventHandler _eventHandler;
};
