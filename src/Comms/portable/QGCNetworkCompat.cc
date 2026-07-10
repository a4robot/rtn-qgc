#include "QGCNetworkCompat.h"

#include <QtCore/QDebug>
#include <QtCore/QElapsedTimer>
#include <QtCore/QMessageAuthenticationCode>
#include <QtCore/QSocketNotifier>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr int kWriteTimeoutMs = 2000;   ///< same ceiling as QGCSerialPortCompat's writeData()
constexpr qint64 kDrainChunk = 16 * 1024;

QAbstractSocket::SocketError errnoToSocketError(int err)
{
    switch (err) {
    case ECONNREFUSED:
        return QAbstractSocket::ConnectionRefusedError;
    case ECONNRESET:
    case EPIPE:
        return QAbstractSocket::RemoteHostClosedError;
    case EACCES:
    case EPERM:
        return QAbstractSocket::SocketAccessError;
    case EADDRINUSE:
        return QAbstractSocket::AddressInUseError;
    case EADDRNOTAVAIL:
        return QAbstractSocket::SocketAddressNotAvailableError;
    case ETIMEDOUT:
        return QAbstractSocket::SocketTimeoutError;
    case EMSGSIZE:
        return QAbstractSocket::DatagramTooLargeError;
    case ENETUNREACH:
    case EHOSTUNREACH:
    case ENETDOWN:
        return QAbstractSocket::NetworkError;
    default:
        return QAbstractSocket::UnknownSocketError;
    }
}

QString errnoString(int err)
{
    return QString::fromLocal8Bit(std::strerror(err));
}

}  // namespace

/*===========================================================================*/
// QAbstractSocket
/*===========================================================================*/

QAbstractSocket::~QAbstractSocket() = default;

void QAbstractSocket::_setState(SocketState state)
{
    if (_state == state) {
        return;
    }
    _state = state;
    emit stateChanged(_state);
}

void QAbstractSocket::_setError(SocketError error, const QString &errorString)
{
    _error = error;
    _errorString = errorString;
    emit errorOccurred(_error);
}

int QAbstractSocket::_openNonBlockingFd(int domain, int type)
{
    const int fd = ::socket(domain, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        _setError(errnoToSocketError(errno), errnoString(errno));
    }
    return fd;
}

bool QAbstractSocket::setSocketOption(SocketOption option, const QVariant &value)
{
    if (_fd < 0) {
        return false;
    }

    switch (option) {
    case LowDelayOption: {
        const int on = value.toInt() ? 1 : 0;
        return ::setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) == 0;
    }
    case KeepAliveOption: {
        const int on = value.toInt() ? 1 : 0;
        return ::setsockopt(_fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) == 0;
    }
    case TypeOfServiceOption: {
        const int tos = value.toInt();
        return ::setsockopt(_fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) == 0;
    }
    case SendBufferSizeSocketOption: {
        const int size = value.toInt();
        return ::setsockopt(_fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) == 0;
    }
    case ReceiveBufferSizeSocketOption: {
        const int size = value.toInt();
        return ::setsockopt(_fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) == 0;
    }
    default:
        return false;
    }
}

QVariant QAbstractSocket::socketOption(SocketOption /*option*/) const
{
    return {};
}

/*===========================================================================*/
// QHostAddress
/*===========================================================================*/

QHostAddress::QHostAddress(const QString &address)
{
    in_addr addr{};
    if (::inet_pton(AF_INET, address.toLatin1().constData(), &addr) == 1) {
        _addr = ntohl(addr.s_addr);
        _isNull = false;
    } else {
        _isNull = true;
    }
}

QHostAddress QHostAddress::_fromSpecial(SpecialAddress address)
{
    QHostAddress result;
    switch (address) {
    case Null:
        return result;
    case Broadcast:
        result._addr = 0xFFFFFFFFu;
        break;
    case LocalHost:
    case LocalHostIPv6:
        result._addr = (127u << 24) | 1u;  // 127.0.0.1 -- no IPv6 support, LocalHostIPv6 maps to v4 loopback
        break;
    case Any:
    case AnyIPv4:
    case AnyIPv6:
        result._addr = 0;
        break;
    }
    result._isNull = false;
    return result;
}

bool QHostAddress::isLoopback() const
{
    return !_isNull && ((_addr >> 24) == 127u);
}

QString QHostAddress::toString() const
{
    if (_isNull) {
        return {};
    }
    in_addr addr{};
    addr.s_addr = htonl(_addr);
    char buf[INET_ADDRSTRLEN] = {};
    if (::inet_ntop(AF_INET, &addr, buf, sizeof(buf)) == nullptr) {
        return {};
    }
    return QString::fromLatin1(buf);
}

size_t qHash(const QHostAddress &address, size_t seed)
{
    return qHash(address.toIPv4Address(), seed);
}

QDebug operator<<(QDebug debug, const QHostAddress &address)
{
    // Same rendering shape as Qt's own operator ("QHostAddress(\"1.2.3.4\")").
    QDebugStateSaver saver(debug);
    debug.nospace() << "QHostAddress(\"" << address.toString() << "\")";
    return debug;
}

/*===========================================================================*/
// QUdpSocket
/*===========================================================================*/

QUdpSocket::QUdpSocket(QObject *parent)
    : QAbstractSocket(parent)
{
}

QUdpSocket::~QUdpSocket()
{
    close();
}

bool QUdpSocket::bind(const QHostAddress &address, quint16 port, BindMode mode)
{
    if (_fd >= 0) {
        close();
    }

    _fd = _openNonBlockingFd(AF_INET, SOCK_DGRAM);
    if (_fd < 0) {
        return false;
    }

    if (mode.testFlag(ReuseAddressHint) || mode.testFlag(ShareAddress)) {
        const int on = 1;
        (void) ::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#ifdef SO_REUSEPORT
        (void) ::setsockopt(_fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
#endif
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(address.toIPv4Address());

    if (::bind(_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        _setError(errnoToSocketError(errno), errnoString(errno));
        ::close(_fd);
        _fd = -1;
        return false;
    }

    socklen_t addrLen = sizeof(addr);
    if (::getsockname(_fd, reinterpret_cast<sockaddr *>(&addr), &addrLen) == 0) {
        _localPort = ntohs(addr.sin_port);
    } else {
        _localPort = port;
    }

    _readNotifier = new QSocketNotifier(_fd, QSocketNotifier::Read, this);
    connect(_readNotifier, &QSocketNotifier::activated, this, &QUdpSocket::_onReadNotifier);

    QIODevice::open(QIODevice::ReadWrite);
    _setState(BoundState);
    return true;
}

void QUdpSocket::close()
{
    if (_readNotifier) {
        _readNotifier->setEnabled(false);
        _readNotifier->deleteLater();
        _readNotifier = nullptr;
    }
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
    QIODevice::close();
    _setState(UnconnectedState);
}

bool QUdpSocket::joinMulticastGroup(const QHostAddress &groupAddress)
{
    if (_fd < 0) {
        return false;
    }
    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = htonl(groupAddress.toIPv4Address());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    return ::setsockopt(_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == 0;
}

bool QUdpSocket::leaveMulticastGroup(const QHostAddress &groupAddress)
{
    if (_fd < 0) {
        return false;
    }
    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = htonl(groupAddress.toIPv4Address());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    return ::setsockopt(_fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) == 0;
}

qint64 QUdpSocket::writeDatagram(const QByteArray &data, const QHostAddress &host, quint16 port)
{
    if (_fd < 0) {
        return -1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(host.toIPv4Address());

    // EAGAIN poll-retry (send buffer momentarily full) -- see QGCNetworkCompat.h's design
    // notes; a datagram either goes out whole or not at all.
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        const qint64 sent = ::sendto(_fd, data.constData(), static_cast<size_t>(data.size()), 0,
                                      reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        if (sent >= 0) {
            return sent;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            _setError(errnoToSocketError(errno), errnoString(errno));
            return -1;
        }
        const int remaining = kWriteTimeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0) {
            _setError(QAbstractSocket::SocketTimeoutError, QStringLiteral("Datagram send timed out"));
            return -1;
        }
        pollfd pfd{};
        pfd.fd = _fd;
        pfd.events = POLLOUT;
        (void) ::poll(&pfd, 1, remaining);
    }
}

bool QUdpSocket::hasPendingDatagrams() const
{
    return pendingDatagramSize() >= 0;
}

qint64 QUdpSocket::pendingDatagramSize() const
{
    if (_fd < 0) {
        return -1;
    }
    char probe[1];
    // MSG_TRUNC makes recv() return the datagram's REAL length even though the probe
    // buffer is 1 byte; MSG_PEEK leaves it queued.
    const ssize_t n = ::recv(_fd, probe, sizeof(probe), MSG_PEEK | MSG_TRUNC | MSG_DONTWAIT);
    if (n < 0) {
        return -1;
    }
    return static_cast<qint64>(n);
}

QNetworkDatagram QUdpSocket::receiveDatagram(qint64 maxSize)
{
    if (_fd < 0) {
        return {};
    }

    qint64 size = maxSize;
    if (size < 0) {
        size = pendingDatagramSize();
        if (size < 0) {
            return {};
        }
    }

    QByteArray buffer(static_cast<qsizetype>(size), Qt::Uninitialized);
    QHostAddress sender;
    quint16 senderPort = 0;
    const qint64 received = readDatagram(buffer.data(), buffer.size(), &sender, &senderPort);
    if (received < 0) {
        return {};
    }
    buffer.resize(static_cast<qsizetype>(received));
    return QNetworkDatagram(buffer, sender, senderPort);
}

qint64 QUdpSocket::readDatagram(char *data, qint64 maxSize, QHostAddress *host, quint16 *port)
{
    if (_fd < 0) {
        return -1;
    }
    sockaddr_in addr{};
    socklen_t addrLen = sizeof(addr);
    const ssize_t n = ::recvfrom(_fd, data, static_cast<size_t>(maxSize), MSG_DONTWAIT,
                                  reinterpret_cast<sockaddr *>(&addr), &addrLen);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            _setError(errnoToSocketError(errno), errnoString(errno));
        }
        return -1;
    }
    if (host) {
        *host = QHostAddress(ntohl(addr.sin_addr.s_addr));
    }
    if (port) {
        *port = ntohs(addr.sin_port);
    }
    return static_cast<qint64>(n);
}

qint64 QUdpSocket::readData(char *data, qint64 maxSize)
{
    if (_fd < 0) {
        return -1;
    }
    const ssize_t n = ::recv(_fd, data, static_cast<size_t>(maxSize), MSG_DONTWAIT);
    if (n < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    }
    return static_cast<qint64>(n);
}

qint64 QUdpSocket::writeData(const char * /*data*/, qint64 /*maxSize*/)
{
    // Connectionless: nothing in this tree writes through the QIODevice base API (always
    // uses writeDatagram() with an explicit destination) -- see QGCNetworkCompat.h.
    return -1;
}

qint64 QUdpSocket::bytesAvailable() const
{
    qint64 queued = 0;
    if (_fd >= 0) {
        int n = 0;
        if (::ioctl(_fd, FIONREAD, &n) == 0) {
            queued = n;
        }
    }
    return QIODevice::bytesAvailable() + queued;
}

void QUdpSocket::_onReadNotifier()
{
    emit readyRead();
}

/*===========================================================================*/
// QTcpSocket
/*===========================================================================*/

QTcpSocket::QTcpSocket(QObject *parent)
    : QAbstractSocket(parent)
{
}

QTcpSocket::~QTcpSocket()
{
    if (_fd >= 0) {
        disconnectFromHost();
    }
}

void QTcpSocket::connectToHost(const QString &hostName, quint16 port)
{
    QHostAddress address(hostName);
    if (address.isNull()) {
        const QHostInfo info = QHostInfo::fromName(hostName);
        if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
            _setError(QAbstractSocket::HostNotFoundError, QStringLiteral("Host not found: %1").arg(hostName));
            _setState(UnconnectedState);
            return;
        }
        address = info.addresses().constFirst();
        emit hostFound();
    }
    _peerName = hostName;
    connectToHost(address, port);
}

void QTcpSocket::connectToHost(const QHostAddress &address, quint16 port)
{
    if (_fd >= 0) {
        disconnectFromHost();
    }
    _rxBuffer.clear();

    _fd = _openNonBlockingFd(AF_INET, SOCK_STREAM);
    if (_fd < 0) {
        _setState(UnconnectedState);
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(address.toIPv4Address());

    _setState(HostLookupState);
    _setState(ConnectingState);

    const int rc = ::connect(_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    if (rc == 0) {
        _finishConnect(_fd, address, port);
        return;
    }
    if (errno != EINPROGRESS) {
        _failConnect(errno);
        return;
    }

    // In flight. Completion arrives via waitForConnected() (blocking callers) or the
    // write-readiness notifier below (fire-and-forget callers) -- whichever runs first.
    _peerAddress = address;
    _peerPort = port;
    _connectNotifier = new QSocketNotifier(_fd, QSocketNotifier::Write, this);
    connect(_connectNotifier, &QSocketNotifier::activated, this, &QTcpSocket::_onConnectNotifier);
}

void QTcpSocket::_onConnectNotifier()
{
    if (_connectNotifier) {
        _connectNotifier->setEnabled(false);
        _connectNotifier->deleteLater();
        _connectNotifier = nullptr;
    }
    if (_state != ConnectingState || _fd < 0) {
        return;
    }

    int sockErr = 0;
    socklen_t len = sizeof(sockErr);
    (void) ::getsockopt(_fd, SOL_SOCKET, SO_ERROR, &sockErr, &len);
    if (sockErr != 0) {
        _failConnect(sockErr);
        return;
    }
    _finishConnect(_fd, _peerAddress, _peerPort);
}

bool QTcpSocket::waitForConnected(int msecs)
{
    if (_state == ConnectedState) {
        return true;
    }
    if (_fd < 0 || _state != ConnectingState) {
        return false;
    }

    pollfd pfd{};
    pfd.fd = _fd;
    pfd.events = POLLOUT;

    const int rc = ::poll(&pfd, 1, msecs);
    if (rc <= 0) {
        _setError(QAbstractSocket::SocketTimeoutError, QStringLiteral("Connection timed out"));
        if (_connectNotifier) {
            _connectNotifier->setEnabled(false);
            _connectNotifier->deleteLater();
            _connectNotifier = nullptr;
        }
        ::close(_fd);
        _fd = -1;
        _setState(UnconnectedState);
        return false;
    }

    int sockErr = 0;
    socklen_t len = sizeof(sockErr);
    (void) ::getsockopt(_fd, SOL_SOCKET, SO_ERROR, &sockErr, &len);
    if (_connectNotifier) {
        _connectNotifier->setEnabled(false);
        _connectNotifier->deleteLater();
        _connectNotifier = nullptr;
    }
    if (sockErr != 0) {
        _failConnect(sockErr);
        return false;
    }

    _finishConnect(_fd, _peerAddress, _peerPort);
    return true;
}

void QTcpSocket::_failConnect(int sockErr)
{
    // errorOccurred() + stateChanged(Unconnected), but NOT disconnected() -- matches Qt
    // for connections that never established (see QGCNetworkCompat.h's design notes).
    _setError(errnoToSocketError(sockErr), errnoString(sockErr));
    if (_connectNotifier) {
        _connectNotifier->setEnabled(false);
        _connectNotifier->deleteLater();
        _connectNotifier = nullptr;
    }
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
    _setState(UnconnectedState);
}

void QTcpSocket::_finishConnect(int fd, const QHostAddress &address, quint16 port)
{
    _peerAddress = address;
    _peerPort = port;

    sockaddr_in local{};
    socklen_t localLen = sizeof(local);
    if (::getsockname(fd, reinterpret_cast<sockaddr *>(&local), &localLen) == 0) {
        _localPort = ntohs(local.sin_port);
    }

    _readNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(_readNotifier, &QSocketNotifier::activated, this, &QTcpSocket::_onReadNotifier);

    QIODevice::open(QIODevice::ReadWrite);
    _setState(ConnectedState);
    emit connected();
}

void QTcpSocket::_adoptConnectedFd(int fd, const QHostAddress &peerAddress, quint16 peerPort)
{
    _fd = fd;
    _peerAddress = peerAddress;
    _peerPort = peerPort;

    sockaddr_in local{};
    socklen_t localLen = sizeof(local);
    if (::getsockname(fd, reinterpret_cast<sockaddr *>(&local), &localLen) == 0) {
        _localPort = ntohs(local.sin_port);
    }

    _readNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(_readNotifier, &QSocketNotifier::activated, this, &QTcpSocket::_onReadNotifier);

    QIODevice::open(QIODevice::ReadWrite);
    _state = ConnectedState;  // already connected at creation -- no connected() emission, matches Qt
}

void QTcpSocket::disconnectFromHost()
{
    if (_connectNotifier) {
        _connectNotifier->setEnabled(false);
        _connectNotifier->deleteLater();
        _connectNotifier = nullptr;
    }
    if (_readNotifier) {
        _readNotifier->setEnabled(false);
        _readNotifier->deleteLater();
        _readNotifier = nullptr;
    }

    const bool wasConnected = (_state == ConnectedState);

    if (_fd >= 0) {
        ::shutdown(_fd, SHUT_RDWR);
        ::close(_fd);
        _fd = -1;
    }
    QIODevice::close();
    _rxBuffer.clear();

    _setState(UnconnectedState);
    if (wasConnected) {
        emit disconnected();
    }
}

bool QTcpSocket::waitForDisconnected(int /*msecs*/)
{
    // disconnectFromHost() is synchronous -- see QGCNetworkCompat.h's design notes.
    return _state == UnconnectedState;
}

bool QTcpSocket::_drainSocket(bool *eofOut)
{
    *eofOut = false;
    bool gotData = false;

    for (;;) {
        const qsizetype oldSize = _rxBuffer.size();
        _rxBuffer.resize(oldSize + kDrainChunk);
        const ssize_t n = ::recv(_fd, _rxBuffer.data() + oldSize, kDrainChunk, MSG_DONTWAIT);
        if (n > 0) {
            _rxBuffer.resize(oldSize + n);
            gotData = true;
            continue;
        }
        _rxBuffer.resize(oldSize);
        if (n == 0) {
            *eofOut = true;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            // Treat hard receive errors like EOF: deliver what we have, then tear down.
            _setError(errnoToSocketError(errno), errnoString(errno));
            *eofOut = true;
        }
        break;
    }

    return gotData;
}

void QTcpSocket::_handleEof()
{
    // Same ordering as Qt: any remaining buffered bytes were already announced via
    // readyRead(); now report the close and tear the connection down.
    if (_readNotifier) {
        _readNotifier->setEnabled(false);
    }
    _setError(QAbstractSocket::RemoteHostClosedError, QStringLiteral("Remote host closed the connection"));
    disconnectFromHost();
}

void QTcpSocket::_onReadNotifier()
{
    if (_fd < 0) {
        return;
    }
    bool eof = false;
    const bool gotData = _drainSocket(&eof);
    if (gotData) {
        emit readyRead();
    }
    if (eof) {
        _handleEof();
    }
}

bool QTcpSocket::waitForReadyRead(int msecs)
{
    if (_fd < 0) {
        return false;
    }
    if (!_rxBuffer.isEmpty()) {
        return true;
    }

    pollfd pfd{};
    pfd.fd = _fd;
    pfd.events = POLLIN;
    const int rc = ::poll(&pfd, 1, msecs);
    if (rc <= 0) {
        return false;
    }

    bool eof = false;
    const bool gotData = _drainSocket(&eof);
    if (gotData) {
        emit readyRead();
    }
    if (eof) {
        _handleEof();
    }
    return gotData;
}

qint64 QTcpSocket::readData(char *data, qint64 maxSize)
{
    const qint64 length = qMin<qint64>(_rxBuffer.size(), maxSize);
    if (length > 0) {
        (void) std::memcpy(data, _rxBuffer.constData(), static_cast<size_t>(length));
        _rxBuffer.remove(0, static_cast<qsizetype>(length));
    }
    return length;
}

qint64 QTcpSocket::writeData(const char *data, qint64 maxSize)
{
    if (_fd < 0) {
        return -1;
    }

    // Blocking (poll-based) retry loop -- see QGCNetworkCompat.h's design notes for why
    // this must never return 0 on EAGAIN (TCPWorker's write loop treats 0 as an error).
    qint64 total = 0;
    QElapsedTimer timer;
    timer.start();
    while (total < maxSize) {
        const ssize_t n = ::send(_fd, data + total, static_cast<size_t>(maxSize - total), MSG_NOSIGNAL);
        if (n > 0) {
            total += n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            const int remaining = kWriteTimeoutMs - static_cast<int>(timer.elapsed());
            if (remaining <= 0) {
                break;
            }
            pollfd pfd{};
            pfd.fd = _fd;
            pfd.events = POLLOUT;
            (void) ::poll(&pfd, 1, remaining);
            continue;
        }
        if (total == 0) {
            _setError(errnoToSocketError(errno), errnoString(errno));
            return -1;
        }
        break;
    }

    if (total > 0) {
        emit bytesWritten(total);
    }
    return total;
}

qint64 QTcpSocket::bytesAvailable() const
{
    return QIODevice::bytesAvailable() + _rxBuffer.size();
}

bool QTcpSocket::canReadLine() const
{
    return _rxBuffer.contains('\n') || QIODevice::canReadLine();
}

/*===========================================================================*/
// QTcpServer
/*===========================================================================*/

QTcpServer::QTcpServer(QObject *parent)
    : QObject(parent)
{
}

QTcpServer::~QTcpServer()
{
    close();
}

bool QTcpServer::listen(const QHostAddress &address, quint16 port)
{
    if (_fd >= 0) {
        close();
    }

    _fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (_fd < 0) {
        _errorString = errnoString(errno);
        return false;
    }

    const int on = 1;
    (void) ::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(address.toIPv4Address());

    if (::bind(_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || ::listen(_fd, 8) != 0) {
        _errorString = errnoString(errno);
        ::close(_fd);
        _fd = -1;
        return false;
    }

    socklen_t addrLen = sizeof(addr);
    if (::getsockname(_fd, reinterpret_cast<sockaddr *>(&addr), &addrLen) == 0) {
        _port = ntohs(addr.sin_port);
    } else {
        _port = port;
    }
    _address = address;

    _acceptNotifier = new QSocketNotifier(_fd, QSocketNotifier::Read, this);
    connect(_acceptNotifier, &QSocketNotifier::activated, this, &QTcpServer::_onAcceptNotifier);
    return true;
}

void QTcpServer::close()
{
    if (_acceptNotifier) {
        _acceptNotifier->setEnabled(false);
        _acceptNotifier->deleteLater();
        _acceptNotifier = nullptr;
    }
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
    while (!_pending.isEmpty()) {
        ::close(_pending.dequeue().fd);
    }
    _port = 0;
}

void QTcpServer::_onAcceptNotifier()
{
    bool accepted = false;
    for (;;) {
        sockaddr_in peer{};
        socklen_t peerLen = sizeof(peer);
        const int fd = ::accept4(_fd, reinterpret_cast<sockaddr *>(&peer), &peerLen, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            break;
        }
        PendingConnection conn;
        conn.fd = fd;
        conn.peerAddress = QHostAddress(ntohl(peer.sin_addr.s_addr));
        conn.peerPort = ntohs(peer.sin_port);
        _pending.enqueue(conn);
        accepted = true;
    }
    if (accepted) {
        emit newConnection();
    }
}

QTcpSocket *QTcpServer::nextPendingConnection()
{
    if (_pending.isEmpty()) {
        return nullptr;
    }
    const PendingConnection conn = _pending.dequeue();
    auto *socket = new QTcpSocket(this);
    socket->_adoptConnectedFd(conn.fd, conn.peerAddress, conn.peerPort);
    return socket;
}

/*===========================================================================*/
// QHostInfo
/*===========================================================================*/

QHostInfo QHostInfo::fromName(const QString &name)
{
    QHostInfo result;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *res = nullptr;
    const int rc = ::getaddrinfo(name.toLatin1().constData(), nullptr, &hints, &res);
    if (rc != 0 || res == nullptr) {
        result._error = HostNotFound;
        return result;
    }

    for (addrinfo *cur = res; cur != nullptr; cur = cur->ai_next) {
        if (cur->ai_family == AF_INET) {
            const auto *sin = reinterpret_cast<sockaddr_in *>(cur->ai_addr);
            result._addresses.append(QHostAddress(ntohl(sin->sin_addr.s_addr)));
        }
    }
    ::freeaddrinfo(res);

    if (result._addresses.isEmpty()) {
        result._error = HostNotFound;
    }
    return result;
}

/*===========================================================================*/
// QNetworkInterface
/*===========================================================================*/

QList<QHostAddress> QNetworkInterface::allAddresses()
{
    QList<QHostAddress> result;

    ifaddrs *ifaddr = nullptr;
    if (::getifaddrs(&ifaddr) != 0) {
        return result;
    }

    for (ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        const auto *sin = reinterpret_cast<sockaddr_in *>(ifa->ifa_addr);
        result.append(QHostAddress(ntohl(sin->sin_addr.s_addr)));
    }

    ::freeifaddrs(ifaddr);
    return result;
}

/*===========================================================================*/
// QPasswordDigestor
/*===========================================================================*/

namespace QPasswordDigestor {

// RFC 8018 PBKDF2, built on QMessageAuthenticationCode (QtCore -- HMAC has always lived
// there, not in QtNetwork) instead of OpenSSL. See QGCNetworkCompat.h's design notes for
// why: no openssl-dev/pkgconfig in the build image, and this avoids a new dependency
// entirely. Pinned bit-for-bit against SigningTest::_testPbkdf2DerivationVector.
QByteArray deriveKeyPbkdf2(QCryptographicHash::Algorithm algorithm, const QByteArray &password,
                            const QByteArray &salt, int iterations, quint64 dkLen)
{
    auto hmac = [&](const QByteArray &key, const QByteArray &msg) -> QByteArray {
        QMessageAuthenticationCode mac(algorithm);
        mac.setKey(key);
        mac.addData(msg);
        return mac.result();
    };

    const QByteArray probe = hmac(password, QByteArrayLiteral(""));
    const int hLen = probe.size();
    if (hLen <= 0 || iterations <= 0 || dkLen == 0) {
        return {};
    }

    QByteArray dk;
    dk.reserve(static_cast<qsizetype>(dkLen));

    const quint32 blockCount = static_cast<quint32>((dkLen + static_cast<quint64>(hLen) - 1) / static_cast<quint64>(hLen));
    for (quint32 blockIndex = 1; blockIndex <= blockCount; ++blockIndex) {
        QByteArray blockSalt = salt;
        blockSalt.append(static_cast<char>((blockIndex >> 24) & 0xFF));
        blockSalt.append(static_cast<char>((blockIndex >> 16) & 0xFF));
        blockSalt.append(static_cast<char>((blockIndex >> 8) & 0xFF));
        blockSalt.append(static_cast<char>(blockIndex & 0xFF));

        QByteArray u = hmac(password, blockSalt);
        QByteArray t = u;
        for (int i = 1; i < iterations; ++i) {
            u = hmac(password, u);
            for (int b = 0; b < t.size(); ++b) {
                t[b] = static_cast<char>(t[b] ^ u[b]);
            }
        }
        dk.append(t);
    }

    dk.truncate(static_cast<qsizetype>(dkLen));
    return dk;
}

}  // namespace QPasswordDigestor
