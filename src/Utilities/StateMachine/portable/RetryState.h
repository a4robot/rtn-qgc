#pragma once

#include "QGCState.h"

#include <QtCore/QTimer>

#include <functional>

/// Portable counterpart of ../States/RetryState.h (unchanged behavior).
class RetryState : public QGCState
{
    Q_OBJECT
    Q_DISABLE_COPY(RetryState)

public:
    using Action = std::function<bool()>;

    enum ExhaustedBehavior {
        EmitError,      ///< Emit error() after retries exhausted.
        EmitAdvance     ///< Emit advance() after retries exhausted (skip/continue).
    };
    Q_ENUM(ExhaustedBehavior)

    RetryState(const QString &stateName, QGCStateMachine *machine,
               Action action,
               int maxRetries = 0,
               int retryDelayMsecs = 1000,
               ExhaustedBehavior exhaustedBehavior = EmitError);

    int currentAttempt() const { return _currentAttempt; }
    int maxRetries() const { return _maxRetries; }
    int totalAttempts() const { return _maxRetries + 1; }
    ExhaustedBehavior exhaustedBehavior() const { return _exhaustedBehavior; }
    bool wasSkipped() const { return _wasSkipped; }

signals:
    void retrying(int attempt, int maxAttempts);
    void succeeded();
    void exhausted();
    void skipped();

protected:
    void onEnter() override;

private slots:
    void _executeAction();

private:
    Action _action;
    int _maxRetries = 0;
    int _retryDelayMsecs = 1000;
    ExhaustedBehavior _exhaustedBehavior = EmitError;

    int _currentAttempt = 0;
    bool _wasSkipped = false;
    QTimer _retryTimer;
};
