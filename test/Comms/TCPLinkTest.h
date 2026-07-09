#pragma once

#include "BaseClasses/CommsTest.h"

/// Wave 16 (Q8e/Q8f prep) characterization tests for TCPLink/TCPWorker.
///
/// These pin the CURRENT QTcpSocket-based behavior (connect/disconnect
/// lifecycle, raw pass-through of partial stream reads with no link-layer
/// MAVLink framing/reassembly, and refused-connection error propagation) as
/// the baseline a future QTcpSocket replacement must reproduce.
/// See src/Comms/TCPLink.cc for the implementation being characterized.
class TCPLinkTest : public CommsTest
{
    Q_OBJECT

private slots:
    /// Connect -> disconnect -> (LinkManager-driven) reconnect lifecycle against
    /// a local QTcpServer. Also pins that TCPLink itself has no automatic
    /// reconnect logic: reconnection here is a fresh createConnectedLink() call,
    /// not something the disconnected TCPLink instance does on its own.
    void _testTcpConnectDisconnectReconnectLifecycle();

    /// A MAVLink heartbeat frame written to the server-side socket in two
    /// separate writes must arrive via bytesReceived as the same total bytes,
    /// in order. TCPWorker::_onSocketReadyRead does a raw socket->readAll()
    /// per readyRead - there is NO frame-level reassembly at the link layer;
    /// that is MAVLinkProtocol's job downstream. Pins that contract.
    void _testTcpPartialFrameAcrossTwoWritesReassemblesAtByteLevel();

    /// Connecting to a port nothing is listening on must surface via
    /// LinkInterface::communicationError and leave the link disconnected -
    /// createConnectedLink() itself returns true regardless (the QTcpSocket
    /// connect happens asynchronously on the worker thread).
    void _testTcpRefusedConnectionEmitsCommunicationError();

private:
    /// Disconnects the link and waits until LinkManager has dropped it, so
    /// the next test starts from a clean links() list.
    void _disconnectAndWaitForRemoval(LinkInterface* link);
};
