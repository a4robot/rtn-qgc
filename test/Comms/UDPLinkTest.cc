#include "UDPLinkTest.h"

#include "LinkManager.h"
#include "UDPLink.h"

#include <MAVLinkLib.h>

#include <QtCore/QElapsedTimer>
#include <QtNetwork/QUdpSocket>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

namespace {

/// Encodes a MAVLink v2 HEARTBEAT and returns its raw wire bytes - with the
/// CRC deliberately corrupted. The link layer under test is byte-agnostic
/// (it must pass these through untouched), but a DECODABLE heartbeat would
/// wake the whole vehicle stack downstream: MAVLinkProtocol::receiveBytes
/// decodes it, MultiVehicleManager creates a Vehicle, and QGC starts sending
/// GCS heartbeats/param requests back over the link - polluting the byte
/// stream this test wants to pin (observed in the first Wave 16 run: the
/// echoed bytes contained sysid-255 GCS frames interleaved with the payload).
/// A bad CRC makes mavlink_parse_char return MAVLINK_FRAMING_BAD_CRC, which
/// MAVLinkProtocol silently skips - keeping the test purely at the link layer
/// while still using realistic MAVLink v2 wire framing.
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

/// Binds a throwaway UDP socket to an ephemeral loopback port, reads back the
/// OS-assigned port, then releases the socket so the caller's own bind can
/// reuse the number. Small TOCTOU race, acceptable for a local test.
quint16 reserveEphemeralUdpPort()
{
    QUdpSocket probe;
    if (!probe.bind(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    const quint16 port = probe.localPort();
    probe.close();
    return port;
}

}  // namespace

// Lifetime note (Wave 16 finding, must survive Q8e/Q8f): when a link emits
// disconnected(), LinkManager::_linkDisconnected removes the only shared_ptr
// from its links list and the LinkInterface is destroyed SYNCHRONOUSLY inside
// that slot. Any observer that wants to outlive the disconnect (signal spies,
// raw pointers) must hold its own SharedLinkInterfacePtr via
// LinkManager::sharedLinkInterfacePointerForLink() - the same contract
// production code (Vehicle, MAVLinkProtocol::receiveBytes) follows.

void UDPLinkTest::_disconnectAndWaitForRemoval(LinkInterface* link)
{
    link->disconnect();
    // Link removal from LinkManager happens via the queued disconnected()
    // signal - wait it out, otherwise the next test's CommsTest::init()
    // asserts on a stale links() count.
    QVERIFY_TRUE_WAIT(linkManager()->links().isEmpty(), TestTimeout::mediumMs());
}

void UDPLinkTest::_testUdpEchoRoundTrip()
{
    QUdpSocket peer;
    QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
    const quint16 peerPort = peer.localPort();

    const quint16 linkPort = reserveEphemeralUdpPort();
    QVERIFY(linkPort != 0);

    SharedLinkConfigurationPtr config(new UDPConfiguration(QStringLiteral("UDPLinkTest_EchoRoundTrip")));
    UDPConfiguration* const udpConfig = qobject_cast<UDPConfiguration*>(config.get());
    QVERIFY(udpConfig);
    udpConfig->setDynamic(true);
    udpConfig->setLocalPort(linkPort);
    udpConfig->addHost(QStringLiteral("127.0.0.1"), peerPort);

    QVERIFY(linkManager()->createConnectedLink(config));
    LinkInterface* const link = config->link();
    QVERIFY(link);
    const SharedLinkInterfacePtr sharedLink = linkManager()->sharedLinkInterfacePointerForLink(link);
    QVERIFY(sharedLink);

    // The bind happens asynchronously on the worker thread; a datagram sent
    // before the socket is bound is silently dropped (UDP has no retry), so
    // wait for the bound state before sending anything.
    QVERIFY_TRUE_WAIT(link->isConnected(), TestTimeout::mediumMs());

    const QByteArray payload = makeUndecodableHeartbeatBytes();

    // --- peer -> link: bytesReceived must carry the exact bytes sent ---
    QSignalSpy receivedSpy(link, &LinkInterface::bytesReceived);
    QCOMPARE(peer.writeDatagram(payload, QHostAddress::LocalHost, linkPort), qint64(payload.size()));
    QVERIFY(UnitTest::waitForSignalCount(receivedSpy, 1, TestTimeout::mediumMs(), QStringLiteral("UDPLink::bytesReceived")));

    const QList<QVariant> receivedArgs = receivedSpy.takeFirst();
    QCOMPARE(receivedArgs.at(0).value<LinkInterface*>(), link);
    QCOMPARE(receivedArgs.at(1).toByteArray(), payload);

    // --- link -> peer: writeBytesThreadSafe must reach the configured target
    // host exactly once (the peer is both a configured target and, after its
    // datagram above, a session target - UDPWorker::writeData must dedupe).
    QSignalSpy peerReadySpy(&peer, &QUdpSocket::readyRead);
    link->writeBytesThreadSafe(payload.constData(), payload.size());
    QVERIFY(UnitTest::waitForSignalCount(peerReadySpy, 1, TestTimeout::mediumMs(), QStringLiteral("peer readyRead")));

    QList<QByteArray> echoedDatagrams;
    while (peer.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(peer.pendingDatagramSize()));
        (void) peer.readDatagram(datagram.data(), datagram.size());
        echoedDatagrams.append(datagram);
    }
    QCOMPARE(echoedDatagrams.size(), 1);
    QCOMPARE(echoedDatagrams.constFirst(), payload);

    _disconnectAndWaitForRemoval(link);
}

void UDPLinkTest::_testUdpBackToBackDatagramsAllArrive()
{
    QUdpSocket peer;
    QVERIFY(peer.bind(QHostAddress::LocalHost, 0));

    const quint16 linkPort = reserveEphemeralUdpPort();
    QVERIFY(linkPort != 0);

    SharedLinkConfigurationPtr config(new UDPConfiguration(QStringLiteral("UDPLinkTest_BackToBack")));
    UDPConfiguration* const udpConfig = qobject_cast<UDPConfiguration*>(config.get());
    QVERIFY(udpConfig);
    udpConfig->setDynamic(true);
    udpConfig->setLocalPort(linkPort);

    QVERIFY(linkManager()->createConnectedLink(config));
    LinkInterface* const link = config->link();
    QVERIFY(link);
    const SharedLinkInterfacePtr sharedLink = linkManager()->sharedLinkInterfacePointerForLink(link);
    QVERIFY(sharedLink);

    // See _testUdpEchoRoundTrip: don't send until the socket is bound.
    QVERIFY_TRUE_WAIT(link->isConnected(), TestTimeout::mediumMs());

    QSignalSpy receivedSpy(link, &LinkInterface::bytesReceived);

    // Several distinct small frames (different sysId per packet so a naive
    // "just concatenate" check can't be fooled by identical payloads).
    QByteArray expected;
    static constexpr int kDatagramCount = 5;
    for (uint8_t i = 0; i < kDatagramCount; ++i) {
        const QByteArray datagram = makeUndecodableHeartbeatBytes(uint8_t(10 + i));
        expected += datagram;
        QCOMPARE(peer.writeDatagram(datagram, QHostAddress::LocalHost, linkPort), qint64(datagram.size()));
    }

    // UDPWorker may coalesce these into fewer bytesReceived emissions than
    // datagrams sent (BUFFER_TRIGGER_SIZE / RECEIVE_TIME_LIMIT_MS) - the
    // contract being pinned is "all bytes eventually arrive, in order",
    // not "one signal per datagram".
    QByteArray accumulated;
    QElapsedTimer timer;
    timer.start();
    while (accumulated.size() < expected.size() && timer.elapsed() < TestTimeout::longMs()) {
        if (receivedSpy.isEmpty()) {
            (void) UnitTest::waitForSignalCount(receivedSpy, 1, TestTimeout::mediumMs(),
                                                 QStringLiteral("UDPLink::bytesReceived"));
        }
        while (!receivedSpy.isEmpty()) {
            const QList<QVariant> args = receivedSpy.takeFirst();
            accumulated += args.at(1).toByteArray();
        }
    }

    QCOMPARE(accumulated, expected);

    _disconnectAndWaitForRemoval(link);
}

// Loopback-only (127.0.0.1) - deterministic, no real network access required,
// so this intentionally does NOT carry TestLabel::Network (which check-ci /
// `ctest -LE "Flaky|Network"` exclude). This is meant to run in CI.
UT_REGISTER_TEST(UDPLinkTest, TestLabel::Integration, TestLabel::Comms)
