#include "TCPLinkTest.h"

#include "LinkManager.h"
#include "TCPLink.h"

#include <MAVLinkLib.h>

#include <QtCore/QScopedPointer>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

namespace {

/// Encodes a MAVLink v2 HEARTBEAT and returns its raw wire bytes - with the
/// CRC deliberately corrupted so MAVLinkProtocol (connected to every live
/// link's bytesReceived) silently skips it instead of decoding it into a
/// Vehicle. See UDPLinkTest.cc's makeUndecodableHeartbeatBytes for the full
/// rationale; the link layer itself is byte-agnostic either way.
QByteArray makeUndecodableHeartbeatBytes(uint8_t sysId = 1, uint8_t compId = MAV_COMP_ID_AUTOPILOT1)
{
    mavlink_message_t message{};
    (void) mavlink_msg_heartbeat_pack_chan(sysId, compId, MAVLINK_COMM_0, &message,
                                            MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_PX4, 0, 0, MAV_STATE_ACTIVE);

    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    const uint16_t len = mavlink_msg_to_send_buffer(buf, &message);
    QByteArray bytes(reinterpret_cast<const char*>(buf), len);
    bytes[bytes.size() - 1] = static_cast<char>(~bytes.at(bytes.size() - 1));  // corrupt CRC high byte
    return bytes;
}

}  // namespace

// Note on wait style: signals here can fire while an earlier wait is spinning
// the event loop (e.g. QTcpServer::newConnection lands during the wait for
// LinkInterface::connected). UnitTest::waitForSignal wraps QSignalSpy::wait(),
// which only observes NEW emissions - so these tests use waitForSignalCount(),
// which honors emissions already recorded by the spy.
//
// Lifetime note (Wave 16 finding, must survive Q8e/Q8f): when a link emits
// disconnected(), LinkManager::_linkDisconnected removes the only shared_ptr
// from its links list and the LinkInterface is destroyed SYNCHRONOUSLY inside
// that slot - a raw LinkInterface* dangles immediately, and signal spies
// connected after LinkManager never see disconnected() because the sender
// dies mid-emission. Observers must hold their own SharedLinkInterfacePtr via
// LinkManager::sharedLinkInterfacePointerForLink(), the same contract
// production code (Vehicle, MAVLinkProtocol::receiveBytes) follows. These
// tests do exactly that.

void TCPLinkTest::_disconnectAndWaitForRemoval(LinkInterface* link)
{
    link->disconnect();
    // Link removal from LinkManager happens via the queued disconnected()
    // signal - wait it out, otherwise the next test's CommsTest::init()
    // asserts on a stale links() count.
    QVERIFY_TRUE_WAIT(linkManager()->links().isEmpty(), TestTimeout::mediumMs());
}

void TCPLinkTest::_testTcpConnectDisconnectReconnectLifecycle()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const quint16 serverPort = server.serverPort();

    SharedLinkConfigurationPtr config(new TCPConfiguration(QStringLiteral("TCPLinkTest_Lifecycle")));
    TCPConfiguration* const tcpConfig = qobject_cast<TCPConfiguration*>(config.get());
    QVERIFY(tcpConfig);
    tcpConfig->setDynamic(true);
    tcpConfig->setHost(QStringLiteral("127.0.0.1"));
    tcpConfig->setPort(serverPort);

    // --- connect ---
    QSignalSpy newConnSpy(&server, &QTcpServer::newConnection);
    QVERIFY(linkManager()->createConnectedLink(config));
    LinkInterface* const link = config->link();
    QVERIFY(link);
    const SharedLinkInterfacePtr sharedLink = linkManager()->sharedLinkInterfacePointerForLink(link);
    QVERIFY(sharedLink);

    QSignalSpy connectedSpy(link, &LinkInterface::connected);
    QVERIFY(UnitTest::waitForSignalCount(connectedSpy, 1, TestTimeout::mediumMs(), QStringLiteral("TCPLink::connected")));
    QVERIFY(link->isConnected());

    QVERIFY(UnitTest::waitForSignalCount(newConnSpy, 1, TestTimeout::mediumMs(), QStringLiteral("QTcpServer::newConnection")));
    QScopedPointer<QTcpSocket> serverSide(server.nextPendingConnection());
    QVERIFY(serverSide);

    // --- disconnect ---
    QSignalSpy disconnectedSpy(link, &LinkInterface::disconnected);
    link->disconnect();
    QVERIFY(UnitTest::waitForSignalCount(disconnectedSpy, 1, TestTimeout::mediumMs(), QStringLiteral("TCPLink::disconnected")));
    QVERIFY(!link->isConnected());

    // TCPLink itself has no auto-reconnect: LinkManager drops the disconnected
    // link (config->link() goes null once _linkDisconnected has run), and a
    // fresh TCPLink instance is what re-establishes the connection below.
    QVERIFY_TRUE_WAIT(config->link() == nullptr, TestTimeout::mediumMs());
    QVERIFY_TRUE_WAIT(linkManager()->links().isEmpty(), TestTimeout::mediumMs());

    // --- reconnect: a new createConnectedLink() call on the same config ---
    QSignalSpy newConnSpy2(&server, &QTcpServer::newConnection);
    QVERIFY(linkManager()->createConnectedLink(config));
    LinkInterface* const link2 = config->link();
    QVERIFY(link2);
    QVERIFY(link2 != link);
    const SharedLinkInterfacePtr sharedLink2 = linkManager()->sharedLinkInterfacePointerForLink(link2);
    QVERIFY(sharedLink2);

    QSignalSpy connectedSpy2(link2, &LinkInterface::connected);
    QVERIFY(UnitTest::waitForSignalCount(connectedSpy2, 1, TestTimeout::mediumMs(), QStringLiteral("TCPLink::connected (reconnect)")));
    QVERIFY(link2->isConnected());

    QVERIFY(UnitTest::waitForSignalCount(newConnSpy2, 1, TestTimeout::mediumMs(), QStringLiteral("QTcpServer::newConnection (reconnect)")));
    QScopedPointer<QTcpSocket> serverSide2(server.nextPendingConnection());
    QVERIFY(serverSide2);

    _disconnectAndWaitForRemoval(link2);
}

void TCPLinkTest::_testTcpPartialFrameAcrossTwoWritesReassemblesAtByteLevel()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    SharedLinkConfigurationPtr config(new TCPConfiguration(QStringLiteral("TCPLinkTest_PartialFrame")));
    TCPConfiguration* const tcpConfig = qobject_cast<TCPConfiguration*>(config.get());
    QVERIFY(tcpConfig);
    tcpConfig->setDynamic(true);
    tcpConfig->setHost(QStringLiteral("127.0.0.1"));
    tcpConfig->setPort(server.serverPort());

    QSignalSpy newConnSpy(&server, &QTcpServer::newConnection);
    QVERIFY(linkManager()->createConnectedLink(config));
    LinkInterface* const link = config->link();
    QVERIFY(link);
    const SharedLinkInterfacePtr sharedLink = linkManager()->sharedLinkInterfacePointerForLink(link);
    QVERIFY(sharedLink);

    QSignalSpy connectedSpy(link, &LinkInterface::connected);
    QVERIFY(UnitTest::waitForSignalCount(connectedSpy, 1, TestTimeout::mediumMs(), QStringLiteral("TCPLink::connected")));
    QVERIFY(UnitTest::waitForSignalCount(newConnSpy, 1, TestTimeout::mediumMs(), QStringLiteral("QTcpServer::newConnection")));
    QScopedPointer<QTcpSocket> serverSide(server.nextPendingConnection());
    QVERIFY(serverSide);

    const QByteArray frame = makeUndecodableHeartbeatBytes();
    QVERIFY(frame.size() > 4);
    const qsizetype splitAt = frame.size() / 2;
    const QByteArray chunk1 = frame.left(int(splitAt));
    const QByteArray chunk2 = frame.mid(int(splitAt));

    // First write, and wait for it to fully arrive (readAll()'d and forwarded)
    // BEFORE sending the second half - this is what makes the split
    // deterministic instead of racing the OS's own coalescing of the two
    // write() calls into a single readyRead.
    QSignalSpy receivedSpy(link, &LinkInterface::bytesReceived);
    (void) serverSide->write(chunk1);
    serverSide->flush();
    QVERIFY(UnitTest::waitForSignalCount(receivedSpy, 1, TestTimeout::mediumMs(), QStringLiteral("bytesReceived (chunk1)")));
    const QByteArray firstReceived = receivedSpy.takeFirst().at(1).toByteArray();
    QCOMPARE(firstReceived, chunk1);

    receivedSpy.clear();
    (void) serverSide->write(chunk2);
    serverSide->flush();
    QVERIFY(UnitTest::waitForSignalCount(receivedSpy, 1, TestTimeout::mediumMs(), QStringLiteral("bytesReceived (chunk2)")));
    const QByteArray secondReceived = receivedSpy.takeFirst().at(1).toByteArray();
    QCOMPARE(secondReceived, chunk2);

    // TCPWorker does no MAVLink-level reassembly - what MAVLinkProtocol needs
    // to see whole again, the test reassembles by concatenation here.
    QCOMPARE(firstReceived + secondReceived, frame);

    _disconnectAndWaitForRemoval(link);
}

void TCPLinkTest::_testTcpRefusedConnectionEmitsCommunicationError()
{
    // Reserve then release a loopback port so nothing is listening on it.
    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 refusedPort = probe.serverPort();
    probe.close();

    SharedLinkConfigurationPtr config(new TCPConfiguration(QStringLiteral("TCPLinkTest_Refused")));
    TCPConfiguration* const tcpConfig = qobject_cast<TCPConfiguration*>(config.get());
    QVERIFY(tcpConfig);
    tcpConfig->setDynamic(true);
    tcpConfig->setHost(QStringLiteral("127.0.0.1"));
    tcpConfig->setPort(refusedPort);

    // createConnectedLink() itself only queues the connect attempt on the
    // worker thread and returns true unconditionally for TCP/UDP - the real
    // pass/fail is only observable via the link's own signals afterward.
    // Grab the shared pointer IMMEDIATELY: the refused connect drives
    // disconnected() and LinkManager would otherwise destroy the link while
    // this test still holds a raw pointer to it (SEGV observed in the first
    // Wave 16 run at exactly this spot).
    QVERIFY(linkManager()->createConnectedLink(config));
    LinkInterface* const link = config->link();
    QVERIFY(link);
    const SharedLinkInterfacePtr sharedLink = linkManager()->sharedLinkInterfacePointerForLink(link);
    QVERIFY(sharedLink);

    QSignalSpy errorSpy(link, &LinkInterface::communicationError);
    QVERIFY(UnitTest::waitForSignalCount(errorSpy, 1, TestTimeout::longMs(), QStringLiteral("TCPLink::communicationError")));
    QVERIFY(!link->isConnected());

    // The failed connect drives _onSocketDisconnected -> disconnected() ->
    // LinkManager removal; wait for it so the next test starts clean.
    QVERIFY_TRUE_WAIT(linkManager()->links().isEmpty(), TestTimeout::mediumMs());
}

// Loopback-only (127.0.0.1) - deterministic, no real network access required,
// so this intentionally does NOT carry TestLabel::Network (which check-ci /
// `ctest -LE "Flaky|Network"` exclude). This is meant to run in CI.
UT_REGISTER_TEST(TCPLinkTest, TestLabel::Integration, TestLabel::Comms)
