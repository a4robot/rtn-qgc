#pragma once

#include "WaitStateBase.h"
#include "QGCMAVLink.h"

#include <cstdint>
#include <functional>

class Vehicle;

/// Portable counterpart of ../States/WaitForParamResponseState.h (unchanged
/// behavior).
class WaitForParamResponseState : public WaitStateBase
{
    Q_OBJECT
    Q_DISABLE_COPY(WaitForParamResponseState)

public:
    using Predicate = std::function<bool(const mavlink_message_t &message)>;

    WaitForParamResponseState(QGCStateMachine *machine, int timeoutMsecs,
                              Predicate paramValuePredicate,
                              Predicate paramErrorPredicate);

    uint8_t lastParamError() const { return _lastParamError; }
    QString lastParamErrorString() const { return _lastParamErrorString; }

protected:
    void connectWaitSignal() override;
    void disconnectWaitSignal() override;

private slots:
    void _messageReceived(const mavlink_message_t &message);

private:
    static QString _paramErrorToString(uint8_t errorCode);

    Predicate _paramValuePredicate;
    Predicate _paramErrorPredicate;
    uint8_t _lastParamError = 0;
    QString _lastParamErrorString;
};
