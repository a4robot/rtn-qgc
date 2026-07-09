#pragma once

#include "BaseClasses/CommsTest.h"

/// Wave 16 (Q8f prep) characterization tests for UDPLink/UDPWorker.
///
/// These pin the CURRENT QUdpSocket-based behavior (framing on receive,
/// coalescing of back-to-back datagrams, write-back to configured/session
/// targets) as the baseline a future QUdpSocket replacement must reproduce.
/// See src/Comms/UDPLink.cc for the implementation being characterized.
class UDPLinkTest : public CommsTest
{
    Q_OBJECT

private slots:
    /// A real UDP peer socket sends a MAVLink heartbeat datagram to the UDPLink's
    /// bound port; asserts LinkInterface::bytesReceived fires with byte-exact
    /// framing, then asserts the link can write data back to the configured
    /// target host (round trip).
    void _testUdpEchoRoundTrip();

    /// Several small datagrams sent back-to-back must all eventually arrive via
    /// bytesReceived (UDPWorker may coalesce multiple datagrams into one signal
    /// emission per BUFFER_TRIGGER_SIZE/RECEIVE_TIME_LIMIT_MS) - pins "no bytes
    /// lost", not "one signal per datagram".
    void _testUdpBackToBackDatagramsAllArrive();

private:
    /// Disconnects the link and waits until LinkManager has dropped it, so
    /// the next test starts from a clean links() list.
    void _disconnectAndWaitForRemoval(LinkInterface* link);
};
