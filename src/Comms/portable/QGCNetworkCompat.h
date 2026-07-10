#pragma once

// Portable POSIX-socket reimplementation of the narrow QtNetwork API surface this tree
// actually uses (QHostAddress, QAbstractSocket, QUdpSocket, QTcpSocket, QTcpServer,
// QNetworkDatagram, QHostInfo, QNetworkInterface, QNetworkProxy, QPasswordDigestor),
// selected when QGC_ENABLE_QT_NETWORK=OFF (see src/Comms/CMakeLists.txt and
// cmake/CustomOptions.cmake's QGC_ENABLE_QT_NETWORK comment). Reached via include-path
// shadowing of `<QtNetwork/QHostAddress>` etc. (src/Comms/portable/QtNetwork/*) -- every
// real consumer (UDPLink, TCPLink, UdpIODevice, LinkManager's NMEA UDP socket,
// ADSBTCPLink, VideoManager's Viewpro TCP command socket, APMFirmwarePlugin's Artoo
// handshake ping, ESP8266ComponentController's QHostAddress formatting,
// MAVLinkSigningKeys' QPasswordDigestor use, QGCApplication's --bridge-host QHostAddress,
// WebBridgeServer's listen-address plumbing, and the UDPLinkTest/TCPLinkTest/ADSBTest
// in-test peers) includes the module-style headers unchanged -- zero consumer-side
// #ifdef, same pattern as src/Comms/portable's QGCSerialPortCompat (Q8e) and
// src/Utilities/StateMachine/portable (Q8b).
//
// Scope, deliberately narrow (see STRANGLER_MILESTONES.md Q8f writeup for the full audit):
// IPv4 only (every real caller only ever uses IPv4 -- 127.0.0.1, AnyIPv4, the 224.0.0.1
// MAVLink multicast group; the LocalHostIPv6 enumerator exists but maps to 127.0.0.1, see
// _fromSpecial); no TLS (this link layer is plaintext MAVLink/ADS-B/Viewpro-TCP, never
// HTTPS -- the HTTP(S)-heavy consumers -- Terrain tile fetch, QtLocationPlugin's map
// providers, NTRIP, QGCFileDownload, MAVLinkLogManager upload, VehicleCameraControl's
// definition-XML download -- are gated behind this same flag and degrade to a documented
// "unavailable in this build" stub instead of being ported; they need a real async HTTP
// client + TLS, which is out of scope for this wave).
//
// Design notes / documented simplifications vs real QtNetwork:
//  - Readiness notification uses QSocketNotifier (QtCore, not QtNetwork) on the raw fd --
//    exactly the same event-loop-integration primitive QGCSerialPortCompat's QSerialPort
//    already relies on, so this works unmodified inside the existing per-link worker
//    QThread + its own event loop (UDPLink/TCPLink already moveToThread() their worker).
//  - QTcpSocket mirrors real QAbstractSocket's buffered-read model: the read notifier
//    drains the kernel into an internal receive buffer and then emits readyRead(), so
//    readAll()/canReadLine()/readLine() (ADSBTCPLink's line-based parser) behave the same
//    as with Qt. EOF (peer close) surfaces as RemoteHostClosedError + disconnected(),
//    after any remaining buffered bytes have been delivered -- same ordering as Qt.
//  - connectToHost() resolves the hostname (getaddrinfo, IPv4 only, synchronous -- real
//    QHostInfo::fromName is blocking too) and issues a non-blocking connect(2).
//    Completion is delivered EITHER via waitForConnected() (TCPWorker's blocking style)
//    OR asynchronously via a write-readiness notifier (ADSBTCPLink/VideoManager/
//    APMFirmwarePlugin's fire-and-forget style) -- whichever comes first.
//  - A *failed* connect emits errorOccurred() + stateChanged(UnconnectedState) but NOT
//    disconnected(), matching Qt (disconnected() is only for connections that were
//    established). TCPWorker's refused-connect path relies on this split.
//  - writeData()/writeDatagram() retry EAGAIN with a poll(2) loop (~2s ceiling), matching
//    QGCSerialPortCompat's writeData(): by the time write() returns, every byte reached
//    the kernel buffer, so flush() is a no-op and waitForBytesWritten() finds nothing
//    pending. Real QAbstractSocket buffers unboundedly instead; nothing in this tree
//    depends on that distinction (TCPWorker already loops on partial writes), but a
//    0-return on EAGAIN would trip TCPWorker's "Write Returned 0 Bytes" error path, hence
//    the retry loop rather than an early return.
//  - QUdpSocket::receiveDatagram()/readDatagram() use recvfrom(2); pendingDatagramSize()/
//    hasPendingDatagrams() use MSG_PEEK+MSG_TRUNC to size the next datagram without
//    consuming it, matching QUdpSocket's semantics for this tree's polling loops
//    (UDPWorker::_onSocketReadyRead, UdpIODevice::_readAvailableData).
//  - QPasswordDigestor::deriveKeyPbkdf2() is a from-scratch RFC 8018 PBKDF2 built on
//    QMessageAuthenticationCode (QtCore, not QtNetwork -- HMAC has always lived in QtCore)
//    instead of OpenSSL: the build image has no openssl-dev/pkgconfig, and QtCore already
//    gives an HMAC primitive, so no new vendored/system dependency is needed at all.
//    Pinned bit-for-bit against test/MAVLink/Signing/SigningTest::
//    _testPbkdf2DerivationVector's independently-computed vector.
//  - No proxy support: QAbstractSocket::setProxy() is accepted and ignored (the only call
//    site, UDPWorker::setupSocket, sets NoProxy anyway -- i.e. asks for exactly the
//    behavior this compat always has).

#include <QtCore/QByteArray>
#include <QtCore/QCryptographicHash>
#include <QtCore/QIODevice>
#include <QtCore/QList>
#include <QtCore/QQueue>
#include <QtCore/QSet>
#include <QtCore/QString>

#include <cstdint>

class QSocketNotifier;

/*===========================================================================*/

class QAbstractSocket : public QIODevice
{
    Q_OBJECT

public:
    enum SocketState {
        UnconnectedState,
        HostLookupState,
        ConnectingState,
        ConnectedState,
        BoundState,
        ListeningState,
        ClosingState,
    };
    Q_ENUM(SocketState)

    enum SocketError {
        ConnectionRefusedError,
        RemoteHostClosedError,
        HostNotFoundError,
        SocketAccessError,
        SocketResourceError,
        SocketTimeoutError,
        DatagramTooLargeError,
        NetworkError,
        AddressInUseError,
        SocketAddressNotAvailableError,
        UnsupportedSocketOperationError,
        UnfinishedSocketOperationError,
        ProxyAuthenticationRequiredError,
        SslHandshakeFailedError,
        ProxyConnectionRefusedError,
        ProxyConnectionClosedError,
        ProxyConnectionTimeoutError,
        ProxyNotFoundError,
        ProxyProtocolError,
        OperationError,
        SslInternalError,
        SslInvalidUserDataError,
        TemporaryError,
        UnknownSocketError = -1,
    };
    Q_ENUM(SocketError)

    enum SocketOption {
        LowDelayOption,
        KeepAliveOption,
        MulticastTtlOption,
        MulticastLoopbackOption,
        TypeOfServiceOption,
        SendBufferSizeSocketOption,
        ReceiveBufferSizeSocketOption,
        PathMtuSocketOption,
    };
    Q_ENUM(SocketOption)

    enum BindFlag {
        DefaultForPlatform = 0x0,
        ShareAddress = 0x1,
        DontShareAddress = 0x2,
        ReuseAddressHint = 0x4,
    };
    Q_DECLARE_FLAGS(BindMode, BindFlag)

    enum NetworkLayerProtocol {
        IPv4Protocol,
        IPv6Protocol,
        AnyIPProtocol,
        UnknownNetworkLayerProtocol = -1,
    };
    Q_ENUM(NetworkLayerProtocol)

    explicit QAbstractSocket(QObject *parent = nullptr) : QIODevice(parent) {}
    ~QAbstractSocket() override;

    SocketState state() const { return _state; }
    SocketError error() const { return _error; }
    QString errorString() const { return _errorString; }

    bool setSocketOption(SocketOption option, const QVariant &value);
    QVariant socketOption(SocketOption option) const;

    quint16 localPort() const { return _localPort; }
    QString peerName() const { return _peerName; }

    /// Writes are synchronous (poll-retry into the kernel buffer, see this file's design
    /// notes) -- nothing is ever left pending to flush.
    bool flush() { return _fd >= 0; }

    // No real proxy support -- accepted and ignored (matches the ghost's "localhost-only,
    // no proxy" deployment; see UDPWorker::setupSocket's QNetworkProxy::NoProxy call site).
    void setProxy(int /*proxyType*/) {}

signals:
    void connected();
    void disconnected();
    void errorOccurred(QAbstractSocket::SocketError socketError);
    void stateChanged(QAbstractSocket::SocketState socketState);
    void hostFound();
    void bytesWritten(qint64 bytes);

protected:
    void _setState(SocketState state);
    void _setError(SocketError error, const QString &errorString);
    int _openNonBlockingFd(int domain, int type);

    int _fd = -1;
    QSocketNotifier *_readNotifier = nullptr;
    SocketState _state = UnconnectedState;
    SocketError _error = UnknownSocketError;
    QString _errorString;
    quint16 _localPort = 0;
    QString _peerName;
};
Q_DECLARE_OPERATORS_FOR_FLAGS(QAbstractSocket::BindMode)

/*===========================================================================*/

class QHostAddress
{
public:
    enum SpecialAddress {
        Null,
        Broadcast,
        LocalHost,
        LocalHostIPv6,
        Any,
        AnyIPv4,
        AnyIPv6,
    };

    QHostAddress() = default;
    QHostAddress(SpecialAddress address) { *this = _fromSpecial(address); }  // NOLINT(google-explicit-constructor) -- matches Qt's implicit-conversion API
    explicit QHostAddress(const QString &address);
    explicit QHostAddress(quint32 ipv4Addr) : _addr(ipv4Addr), _isNull(false) {}

    bool isNull() const { return _isNull; }
    bool isLoopback() const;
    QString toString() const;
    quint32 toIPv4Address() const { return _addr; }
    QAbstractSocket::NetworkLayerProtocol protocol() const
    {
        return _isNull ? QAbstractSocket::UnknownNetworkLayerProtocol : QAbstractSocket::IPv4Protocol;
    }

    bool operator==(const QHostAddress &other) const { return _isNull == other._isNull && _addr == other._addr; }
    bool operator!=(const QHostAddress &other) const { return !(*this == other); }

private:
    static QHostAddress _fromSpecial(SpecialAddress address);

    quint32 _addr = 0;  ///< host byte order
    bool _isNull = true;
};

size_t qHash(const QHostAddress &address, size_t seed = 0);

class QDebug;
QDebug operator<<(QDebug debug, const QHostAddress &address);

/*===========================================================================*/

class QNetworkDatagram
{
public:
    QNetworkDatagram() = default;
    QNetworkDatagram(const QByteArray &data, const QHostAddress &sender, quint16 senderPort)
        : _data(data), _senderAddress(sender), _senderPort(senderPort), _isNull(false)
    {}

    bool isNull() const { return _isNull; }
    QByteArray data() const { return _data; }
    QHostAddress senderAddress() const { return _senderAddress; }
    quint16 senderPort() const { return _senderPort; }

private:
    QByteArray _data;
    QHostAddress _senderAddress;
    quint16 _senderPort = 0;
    bool _isNull = true;
};

/*===========================================================================*/

class QUdpSocket : public QAbstractSocket
{
    Q_OBJECT

public:
    explicit QUdpSocket(QObject *parent = nullptr);
    ~QUdpSocket() override;

    bool bind(const QHostAddress &address, quint16 port, BindMode mode = DefaultForPlatform);
    void close() override;
    bool isValid() const { return _fd >= 0; }

    bool joinMulticastGroup(const QHostAddress &groupAddress);
    bool leaveMulticastGroup(const QHostAddress &groupAddress);

    qint64 writeDatagram(const QByteArray &data, const QHostAddress &host, quint16 port);

    bool hasPendingDatagrams() const;
    qint64 pendingDatagramSize() const;
    QNetworkDatagram receiveDatagram(qint64 maxSize = -1);
    qint64 readDatagram(char *data, qint64 maxSize, QHostAddress *host = nullptr, quint16 *port = nullptr);

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 maxSize) override;

private slots:
    void _onReadNotifier();
};

/*===========================================================================*/

class QTcpSocket : public QAbstractSocket
{
    Q_OBJECT

public:
    explicit QTcpSocket(QObject *parent = nullptr);
    ~QTcpSocket() override;

    void connectToHost(const QString &hostName, quint16 port);
    void connectToHost(const QHostAddress &address, quint16 port);
    bool waitForConnected(int msecs = 30000);

    void disconnectFromHost();
    bool waitForDisconnected(int msecs = 30000);

    bool isOpen() const { return _fd >= 0; }

    QHostAddress peerAddress() const { return _peerAddress; }
    quint16 peerPort() const { return _peerPort; }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;
    bool canReadLine() const override;
    bool waitForReadyRead(int msecs = 30000) override;

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 maxSize) override;

private slots:
    void _onReadNotifier();
    void _onConnectNotifier();

private:
    friend class QTcpServer;

    /// Server side: wrap an already-connected fd from accept(2) (QTcpServer::
    /// nextPendingConnection). Matches Qt in NOT emitting connected() for these.
    void _adoptConnectedFd(int fd, const QHostAddress &peerAddress, quint16 peerPort);

    void _finishConnect(int fd, const QHostAddress &address, quint16 port);
    void _failConnect(int sockErr);
    /// Drains the kernel receive queue into _rxBuffer. Returns true if new bytes arrived;
    /// sets *eofOut when the peer has closed (recv() == 0).
    bool _drainSocket(bool *eofOut);
    void _handleEof();

    QHostAddress _peerAddress;
    quint16 _peerPort = 0;
    QByteArray _rxBuffer;
    QSocketNotifier *_connectNotifier = nullptr;
};

/*===========================================================================*/

/// Loopback test-peer server (UDPLinkTest/TCPLinkTest/ADSBTest and the TcpServerTest
/// fixture) -- no production code in this tree listens on TCP.
class QTcpServer : public QObject
{
    Q_OBJECT

public:
    explicit QTcpServer(QObject *parent = nullptr);
    ~QTcpServer() override;

    bool listen(const QHostAddress &address = QHostAddress(QHostAddress::Any), quint16 port = 0);
    void close();
    bool isListening() const { return _fd >= 0; }

    quint16 serverPort() const { return _port; }
    QHostAddress serverAddress() const { return _address; }

    bool hasPendingConnections() const { return !_pending.isEmpty(); }
    QTcpSocket *nextPendingConnection();

    QString errorString() const { return _errorString; }

signals:
    void newConnection();

private slots:
    void _onAcceptNotifier();

private:
    struct PendingConnection {
        int fd = -1;
        QHostAddress peerAddress;
        quint16 peerPort = 0;
    };

    int _fd = -1;
    quint16 _port = 0;
    QHostAddress _address;
    QString _errorString;
    QSocketNotifier *_acceptNotifier = nullptr;
    QQueue<PendingConnection> _pending;
};

/*===========================================================================*/

class QHostInfo
{
public:
    enum HostInfoError {
        NoError,
        HostNotFound,
        UnknownError,
    };

    QList<QHostAddress> addresses() const { return _addresses; }
    HostInfoError error() const { return _error; }

    static QHostInfo fromName(const QString &name);

private:
    QList<QHostAddress> _addresses;
    HostInfoError _error = NoError;
};

/*===========================================================================*/

namespace QNetworkInterface {
    QList<QHostAddress> allAddresses();
}

/*===========================================================================*/

// Only QNetworkProxy::NoProxy is ever constructed in this tree (UDPWorker::setupSocket) --
// a bare enum accepted by QAbstractSocket::setProxy(int) is sufficient; no class needed.
namespace QNetworkProxy {
    enum ProxyType {
        DefaultProxy,
        Socks5Proxy,
        NoProxy,
        HttpProxy,
        HttpCachingProxy,
        FtpCachingProxy,
    };
}

/*===========================================================================*/

namespace QPasswordDigestor {
    QByteArray deriveKeyPbkdf2(QCryptographicHash::Algorithm algorithm, const QByteArray &password,
                                const QByteArray &salt, int iterations, quint64 dkLen);
}
