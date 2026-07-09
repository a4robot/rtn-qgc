#include "TerrainTileFetcherNetworkTest.h"

#include "AppSettings.h"
#include "Fixtures/RAIIFixtures.h"
#include "QGCMapUrlEngine.h"
#include "QGCNetworkHelper.h"
#include "SettingsManager.h"
#include "TerrainTileFetcher.h"

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

namespace {

/// Minimal single-shot loopback HTTP/1.1 responder: accepts one connection
/// and, once anything arrives on it (the request), writes back a canned raw
/// response and closes. In "hang" mode it accepts the connection and never
/// writes anything, to drive the client-side transfer timeout.
class LocalHttpResponder : public QObject
{
    Q_OBJECT

public:
    explicit LocalHttpResponder(QObject* parent = nullptr) : QObject(parent)
    {
        connect(&_server, &QTcpServer::newConnection, this, &LocalHttpResponder::_onNewConnection);
    }

    bool listen() { return _server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return _server.serverPort(); }

    /// Respond to the next accepted connection with this raw HTTP response.
    void respondWith(const QByteArray& rawHttpResponse)
    {
        _hang = false;
        _response = rawHttpResponse;
    }

    /// Accept the next connection but never write a response (timeout drill).
    void hangOnNextConnection() { _hang = true; }

private slots:
    void _onNewConnection()
    {
        while (_server.hasPendingConnections()) {
            QTcpSocket* const socket = _server.nextPendingConnection();
            socket->setParent(this);

            if (_hang) {
                // Deliberately unresponsive - let the socket sit open.
                continue;
            }

            const QByteArray response = _response;
            connect(socket, &QTcpSocket::readyRead, socket, [socket, response]() {
                (void) socket->write(response);
                socket->flush();
                socket->disconnectFromHost();
            });
        }
    }

private:
    QTcpServer _server;
    QByteArray _response;
    bool _hang = false;
};

QByteArray httpOkResponse(const QByteArray& body)
{
    return QByteArrayLiteral("HTTP/1.1 200 OK\r\n"
                              "Content-Type: image/png\r\n"
                              "Connection: close\r\n"
                              "Content-Length: ") +
           QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n") + body;
}

QByteArray httpNotFoundResponse()
{
    return QByteArrayLiteral("HTTP/1.1 404 Not Found\r\n"
                              "Connection: close\r\n"
                              "Content-Length: 0\r\n\r\n");
}

/// Bytes that satisfy MapProvider::getImageFormat()'s PNG magic-byte check
/// without needing to be a structurally valid PNG - TerrainTileFetcher only
/// inspects the leading signature for non-elevation providers.
QByteArray cannedTileBody()
{
    static const QByteArray kPngSignature("\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8);
    return kPngSignature + QByteArrayLiteral("wave16-terrain-network-characterization");
}

/// Cache the (potentially slow / environment-dependent) reachability probe
/// once per process, per test/TESTING.md's "expensive SKIP paths" guidance.
bool internetProbeAvailable()
{
    static const bool available = QGCNetworkHelper::isInternetAvailable();
    return available;
}

int customUrlMapId()
{
    return UrlFactory::getQtMapIdFromProviderType(QStringLiteral("CustomURL Custom"));
}

}  // namespace

void TerrainTileFetcherNetworkTest::_testNetworkFetchSuccessPath()
{
    if (!internetProbeAvailable()) {
        QSKIP("QNetworkInformation reachability backend reports unavailable in this sandbox; "
              "network-fetch E2E characterization skipped (hermetic request-shape coverage "
              "lives in UrlFactoryTest::_testGetTileNetworkRequest*).");
    }

    LocalHttpResponder responder;
    QVERIFY(responder.listen());
    const QByteArray body = cannedTileBody();
    responder.respondWith(httpOkResponse(body));

    TestFixtures::SettingsFixture settings;
    settings.setFactValue(SettingsManager::instance()->appSettings()->disableAllPersistence(), true);
    settings.setFactValue(SettingsManager::instance()->appSettings()->customURL(),
                          QStringLiteral("http://127.0.0.1:%1/{z}/{x}/{y}.png").arg(responder.port()));

    QNetworkAccessManager networkManager;
    TerrainTileFetcher fetcher(&networkManager, QStringLiteral("CustomURL Custom"), customUrlMapId(), 4101, 4102, 12);
    QSignalSpy finishedSpy(&fetcher, &TerrainTileFetcher::finished);
    QVERIFY(fetcher.init());
    QVERIFY(UnitTest::waitForSignal(finishedSpy, TestTimeout::longMs(), QStringLiteral("TerrainTileFetcher::finished")));

    QVERIFY2(!fetcher.hasError(), qPrintable(fetcher.errorString()));
    QCOMPARE(fetcher.tileData(), body);
}

void TerrainTileFetcherNetworkTest::_testNetworkFetch404SurfacesAsError()
{
    if (!internetProbeAvailable()) {
        QSKIP("QNetworkInformation reachability backend reports unavailable in this sandbox; "
              "network-fetch E2E characterization skipped (hermetic request-shape coverage "
              "lives in UrlFactoryTest::_testGetTileNetworkRequest*).");
    }

    LocalHttpResponder responder;
    QVERIFY(responder.listen());
    responder.respondWith(httpNotFoundResponse());

    TestFixtures::SettingsFixture settings;
    settings.setFactValue(SettingsManager::instance()->appSettings()->disableAllPersistence(), true);
    settings.setFactValue(SettingsManager::instance()->appSettings()->customURL(),
                          QStringLiteral("http://127.0.0.1:%1/{z}/{x}/{y}.png").arg(responder.port()));

    QNetworkAccessManager networkManager;
    TerrainTileFetcher fetcher(&networkManager, QStringLiteral("CustomURL Custom"), customUrlMapId(), 4201, 4202, 12);
    QSignalSpy finishedSpy(&fetcher, &TerrainTileFetcher::finished);
    QVERIFY(fetcher.init());
    QVERIFY(UnitTest::waitForSignal(finishedSpy, TestTimeout::longMs(), QStringLiteral("TerrainTileFetcher::finished")));

    QVERIFY(fetcher.hasError());
    QVERIFY(!fetcher.errorString().isEmpty());
    QVERIFY(fetcher.tileData().isEmpty());
}

void TerrainTileFetcherNetworkTest::_testNetworkFetchTimeoutSurfacesAsError()
{
    if (!internetProbeAvailable()) {
        QSKIP("QNetworkInformation reachability backend reports unavailable in this sandbox; "
              "network-fetch E2E characterization skipped (hermetic request-shape coverage "
              "lives in UrlFactoryTest::_testGetTileNetworkRequest*).");
    }

    LocalHttpResponder responder;
    QVERIFY(responder.listen());
    responder.hangOnNextConnection();

    TestFixtures::SettingsFixture settings;
    settings.setFactValue(SettingsManager::instance()->appSettings()->disableAllPersistence(), true);
    settings.setFactValue(SettingsManager::instance()->appSettings()->customURL(),
                          QStringLiteral("http://127.0.0.1:%1/{z}/{x}/{y}.png").arg(responder.port()));

    QNetworkAccessManager networkManager;
    TerrainTileFetcher fetcher(&networkManager, QStringLiteral("CustomURL Custom"), customUrlMapId(), 4301, 4302, 12);
    QSignalSpy finishedSpy(&fetcher, &TerrainTileFetcher::finished);
    QVERIFY(fetcher.init());

    // getTileNetworkRequest() bakes in a 10s transferTimeout(); give it real
    // headroom above that rather than the usual "long" timeout budget.
    QVERIFY(UnitTest::waitForSignal(finishedSpy, 20000, QStringLiteral("TerrainTileFetcher::finished (timeout path)")));

    QVERIFY(fetcher.hasError());
    QVERIFY(!fetcher.errorString().isEmpty());
}

UT_REGISTER_TEST(TerrainTileFetcherNetworkTest, TestLabel::Integration, TestLabel::Terrain, TestLabel::Slow)

#include "TerrainTileFetcherNetworkTest.moc"
