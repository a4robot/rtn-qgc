#pragma once

#include "QGCState.h"

#include <QtCore/QTimer>

#include <functional>

class QGCStateMachine;

/// Portable counterpart of ../Helpers/ErrorRecoveryBuilder.h. Ported as-is
/// per AUDIT.md ("port as-is, no redesign needed") -- only the QState base
/// needs replacing.
class ErrorRecoveryBuilder
{
public:
    using Action = std::function<bool()>;
    using VoidAction = std::function<void()>;

    enum ExhaustedBehavior {
        EmitError,
        EmitAdvance,
        LogAndError,
        LogAndAdvance
    };

    ErrorRecoveryBuilder(QGCStateMachine *machine, const QString &stateName);

    ErrorRecoveryBuilder &withAction(Action action);
    ErrorRecoveryBuilder &retry(int maxRetries, int delayMsecs = 1000);
    ErrorRecoveryBuilder &withFallback(Action fallback);
    ErrorRecoveryBuilder &withRollback(VoidAction rollback);
    ErrorRecoveryBuilder &onExhausted(ExhaustedBehavior behavior);
    ErrorRecoveryBuilder &withTimeout(int timeoutMsecs);

    QGCState *build();

private:
    QGCStateMachine *_machine;
    QString _stateName;
    Action _action;
    Action _fallback;
    VoidAction _rollback;
    int _maxRetries = 0;
    int _retryDelayMsecs = 1000;
    int _timeoutMsecs = 0;
    ExhaustedBehavior _exhaustedBehavior = EmitError;
};

/// \brief The state created by ErrorRecoveryBuilder
class ErrorRecoveryState : public QGCState
{
    Q_OBJECT
    Q_DISABLE_COPY(ErrorRecoveryState)

public:
    using Action = std::function<bool()>;
    using VoidAction = std::function<void()>;

    ErrorRecoveryState(const QString &stateName, QGCStateMachine *machine);

    void setAction(Action action) { _action = std::move(action); }
    void setFallback(Action fallback) { _fallback = std::move(fallback); }
    void setRollback(VoidAction rollback) { _rollback = std::move(rollback); }
    void setRetry(int maxRetries, int delayMsecs);
    void setTimeout(int timeoutMsecs);
    void setExhaustedBehavior(ErrorRecoveryBuilder::ExhaustedBehavior behavior);

    QString successPhase() const { return _successPhase; }

signals:
    void retrying(int attempt, int maxAttempts);
    void tryingFallback();
    void rollingBack();
    void succeeded();
    void exhausted();

protected:
    void onEnter() override;

private slots:
    void _executeAction();
    void _onTimeout();

private:
    void _handleFailure();
    void _handleExhausted();

    Action _action;
    Action _fallback;
    VoidAction _rollback;
    int _maxRetries = 0;
    int _retryDelayMsecs = 1000;
    int _timeoutMsecs = 0;
    ErrorRecoveryBuilder::ExhaustedBehavior _exhaustedBehavior = ErrorRecoveryBuilder::EmitError;

    int _currentAttempt = 0;
    bool _triedFallback = false;
    QString _successPhase;
    QTimer _retryTimer;
    QTimer _timeoutTimer;
};
