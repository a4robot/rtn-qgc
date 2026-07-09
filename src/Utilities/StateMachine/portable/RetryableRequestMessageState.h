#pragma once

#include "WaitStateBase.h"
#include "MAVLinkEnums.h"
#include "MAVLinkMessageType.h"
#include "VehicleTypes.h"

class Vehicle;

#include <cstdint>
#include <functional>

/// Portable counterpart of ../States/RetryableRequestMessageState.h
/// (unchanged behavior).
class RetryableRequestMessageState : public WaitStateBase
{
    Q_OBJECT
    Q_DISABLE_COPY(RetryableRequestMessageState)

public:
    using MessageHandler = std::function<void(Vehicle *vehicle, const mavlink_message_t &message)>;
    using FailureHandler = std::function<void(VehicleTypes::RequestMessageResultHandlerFailureCode_t failureCode, MAV_RESULT result)>;
    using SkipPredicate = std::function<bool()>;

    RetryableRequestMessageState(const QString &stateName,
                                  QGCStateMachine *machine,
                                  uint32_t messageId,
                                  MessageHandler messageHandler = nullptr,
                                  int maxRetries = 1,
                                  int compId = MAV_COMP_ID_AUTOPILOT1,
                                  int timeoutMsecs = 5000);

    void setFailureHandler(FailureHandler handler) { _failureHandler = std::move(handler); }
    void setSkipPredicate(SkipPredicate predicate) { _skipPredicate = std::move(predicate); }
    void setFailOnMaxRetries(bool fail) { _failOnMaxRetries = fail; }

    VehicleTypes::RequestMessageResultHandlerFailureCode_t lastFailureCode() const { return _lastFailureCode; }
    MAV_RESULT lastResult() const { return _lastResult; }
    int retryCount() const { return _retryCount; }
    int maxRetries() const { return _maxRetries; }

signals:
    void messageReceived(const mavlink_message_t &message);
    void retriesExhausted();

protected:
    void connectWaitSignal() override {}
    void disconnectWaitSignal() override {}
    void onWaitEntered() override;
    void onWaitExited() override;
    void onWaitTimeout() override;

private:
    void _sendRequest();
    void _queueRetry();
    void _handleResult(MAV_RESULT result,
                       VehicleTypes::RequestMessageResultHandlerFailureCode_t failureCode,
                       const mavlink_message_t &message);

    uint32_t _messageId;
    MessageHandler _messageHandler;
    FailureHandler _failureHandler;
    SkipPredicate _skipPredicate;
    int _compId;
    int _maxRetries;
    int _retryCount = 0;
    bool _failOnMaxRetries = false;

    bool _requestActive = false;

    VehicleTypes::RequestMessageResultHandlerFailureCode_t _lastFailureCode = VehicleTypes::RequestMessageNoFailure;
    MAV_RESULT _lastResult = MAV_RESULT_ACCEPTED;
};
