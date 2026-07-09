#pragma once

#include "QGCState.h"
#include "MAVLinkMessageType.h"

#include <cstdint>
#include <functional>

class Vehicle;

/// Portable counterpart of ../States/SendMavlinkMessageState.h (unchanged
/// behavior).
class SendMavlinkMessageState : public QGCState
{
    Q_OBJECT
    Q_DISABLE_COPY(SendMavlinkMessageState)

public:
    using MessageEncoder = std::function<void (uint8_t systemId, uint8_t channel, mavlink_message_t *message)>;

    SendMavlinkMessageState(QGCStateMachine *machine, MessageEncoder encoder, int retryCount);

private slots:
    void _sendMessage();

private:
    MessageEncoder _encoder;
    int _retryCount = 0;
    int _runCount = 0;
};
