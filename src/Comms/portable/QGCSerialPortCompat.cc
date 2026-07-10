// Note on feature-test macros: g++ implicitly defines _GNU_SOURCE for C++ on Linux (even
// with CMAKE_CXX_EXTENSIONS OFF -- that only controls -std=c++20 vs -std=gnu++20 language
// extensions, not libc feature exposure), so cfmakeraw()/TIOCM_*/CRTSCTS/FIONREAD are all
// visible here without any explicit #define. Defining it ourselves trips -Werror
// ("_GNU_SOURCE redefined").
#include "QGCSerialPortCompat.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QAbstractEventDispatcher>
#include <QtCore/QElapsedTimer>
#include <QtCore/QSocketNotifier>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <libudev.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

QGC_LOGGING_CATEGORY(SerialCompatLog, "Comms.SerialPortCompat")

namespace {

struct BaudEntry { qint32 rate; speed_t flag; };

// Every rate SerialConfiguration::supportedBaudRates() (SerialLink.cc) ever offers on
// Linux has a direct glibc B-constant -- see QGCSerialPortCompat.h's design notes for why
// this table (rather than termios2/BOTHER) is sufficient.
constexpr BaudEntry kBaudTable[] = {
    {50, B50}, {75, B75}, {110, B110}, {134, B134}, {150, B150}, {200, B200},
    {300, B300}, {600, B600}, {1200, B1200}, {1800, B1800}, {2400, B2400},
    {4800, B4800}, {9600, B9600}, {19200, B19200}, {38400, B38400},
    {57600, B57600}, {115200, B115200},
#ifdef B230400
    {230400, B230400},
#endif
#ifdef B460800
    {460800, B460800},
#endif
#ifdef B500000
    {500000, B500000},
#endif
#ifdef B576000
    {576000, B576000},
#endif
#ifdef B921600
    {921600, B921600},
#endif
};

bool baudRateToSpeed(qint32 rate, speed_t &speed)
{
    for (const BaudEntry &entry : kBaudTable) {
        if (entry.rate == rate) {
            speed = entry.flag;
            return true;
        }
    }
    return false;
}

constexpr int kWriteTimeoutMs = 2000;

// Real QSerialPort accepts both a bare port name ("ttyUSB0") and a full system location
// ("/dev/ttyUSB0") -- normalize to the full path for open(2)/comparison, matching Qt's
// Linux behavior.
QString systemLocationForPortName(const QString &name)
{
    if (name.isEmpty() || name.startsWith(QLatin1Char('/'))) {
        return name;
    }
    return QStringLiteral("/dev/") + name;
}

} // namespace

/*===========================================================================*/

QSerialPort::QSerialPort(QObject *parent)
    : QIODevice(parent)
{
}

QSerialPort::QSerialPort(const QSerialPortInfo &info, QObject *parent)
    : QIODevice(parent)
{
    setPortName(info.systemLocation());
}

QSerialPort::~QSerialPort()
{
    close();
}

void QSerialPort::setPortName(const QString &name)
{
    // Match real QSerialPort's Linux behavior (pinned by SerialPortDataPathTest::
    // _testOpenClose against the Qt backend): the stored/reported port name is the form
    // WITHOUT the "/dev/" prefix ("/dev/pts/0" -> "pts/0", "/dev/ttyUSB0" -> "ttyUSB0");
    // open() re-derives the full system location via systemLocationForPortName(). This
    // form must agree across backends: LinkManager's _checkPortAvailability and
    // SerialConfiguration::cleanPortDisplayName compare portName()s between QSerialPort
    // and QSerialPortInfo.
    _portName = name;
    if (_portName.startsWith(QStringLiteral("/dev/"))) {
        _portName.remove(0, 5);
    }
}

void QSerialPort::_setError(SerialPortError error, const QString &errorString)
{
    _error = error;
    _errorString = errorString;
    if (error != NoError) {
        emit errorOccurred(error);
    }
}

void QSerialPort::clearError()
{
    _error = NoError;
    _errorString.clear();
}

bool QSerialPort::open(QIODevice::OpenMode mode)
{
    if (isOpen()) {
        return true;
    }

    if (_portName.isEmpty()) {
        _setError(DeviceNotFoundError, QStringLiteral("No port name set"));
        return false;
    }

    const QString location = systemLocationForPortName(_portName);
    const int fd = ::open(location.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        SerialPortError err = UnknownError;
        switch (errno) {
        case EACCES:
        case EBUSY:
            err = PermissionError;
            break;
        case ENOENT:
        case ENXIO:
        case ENODEV:
            err = DeviceNotFoundError;
            break;
        default:
            err = OpenError;
            break;
        }
        _setError(err, QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }

    _fd = fd;

    if (!_applyTermios()) {
        // Not fatal -- SerialWorker::_onPortConnected() (SerialLink.cc) reapplies
        // baud/format explicitly right after connected() fires, same as it does with the
        // real QSerialPort backend, so a first-pass failure here is recoverable. Some
        // fds (e.g. a pty slave in edge cases) also don't support every termios knob.
        qCDebug(SerialCompatLog) << "Initial termios setup failed on" << _portName << std::strerror(errno);
    }

    // The readyRead notifier needs the current thread's event dispatcher. SerialLink's
    // SerialWorker thread has one (QThread::exec); GPS/GPSProvider deliberately does NOT
    // (raw QThread::run with a blocking waitForReadyRead/read loop, never readyRead) --
    // skip the notifier there instead of letting QSocketNotifier warn about it.
    if (QAbstractEventDispatcher::instance()) {
        _readNotifier = new QSocketNotifier(_fd, QSocketNotifier::Read, this);
        connect(_readNotifier, &QSocketNotifier::activated, this, &QSerialPort::_handleReadyRead);
    }

    QIODevice::open(mode);
    clearError();
    return true;
}

void QSerialPort::close()
{
    if (_fd < 0) {
        return;
    }

    delete _readNotifier;
    _readNotifier = nullptr;

    ::close(_fd);
    _fd = -1;

    // QIODevice::close() emits aboutToClose() itself (which SerialWorker relies on for its
    // disconnected() flow) -- do NOT emit it manually here or it fires twice.
    QIODevice::close();
}

bool QSerialPort::_applyTermios()
{
    if (_fd < 0) {
        return false;
    }

    struct termios tio {};
    if (::tcgetattr(_fd, &tio) != 0) {
        return false;
    }

    ::cfmakeraw(&tio);
    tio.c_cflag |= (CLOCAL | CREAD);

    // Data bits
    tio.c_cflag &= ~CSIZE;
    switch (_dataBits) {
    case Data5: tio.c_cflag |= CS5; break;
    case Data6: tio.c_cflag |= CS6; break;
    case Data7: tio.c_cflag |= CS7; break;
    case Data8:
    default:    tio.c_cflag |= CS8; break;
    }

    // Parity (SpaceParity/MarkParity have no direct POSIX termios equivalent and fall back
    // to NoParity -- nothing in-tree ever sets them)
    switch (_parity) {
    case EvenParity:
        tio.c_cflag |= PARENB;
        tio.c_cflag &= ~PARODD;
        break;
    case OddParity:
        tio.c_cflag |= PARENB;
        tio.c_cflag |= PARODD;
        break;
    case NoParity:
    default:
        tio.c_cflag &= ~PARENB;
        break;
    }

    // Stop bits
    if (_stopBits == TwoStop) {
        tio.c_cflag |= CSTOPB;
    } else {
        tio.c_cflag &= ~CSTOPB;
    }

    // Flow control
    tio.c_cflag &= ~CRTSCTS;
    tio.c_iflag &= ~(IXON | IXOFF | IXANY);
    if (_flowControl == HardwareControl) {
        tio.c_cflag |= CRTSCTS;
    } else if (_flowControl == SoftwareControl) {
        tio.c_iflag |= (IXON | IXOFF | IXANY);
    }

    // Baud rate: silently skipped if outside kBaudTable (documented in the header) -- on a
    // pty this ioctl succeeds either way but has no throttling effect regardless.
    speed_t speed;
    if (baudRateToSpeed(_baudRate, speed)) {
        ::cfsetispeed(&tio, speed);
        ::cfsetospeed(&tio, speed);
    }

    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    return (::tcsetattr(_fd, TCSANOW, &tio) == 0);
}

bool QSerialPort::setBaudRate(qint32 baudRate, Direction directions)
{
    Q_UNUSED(directions);
    _baudRate = baudRate;
    if (_fd < 0) {
        return true;
    }
    speed_t speed;
    if (!baudRateToSpeed(baudRate, speed)) {
        qCWarning(SerialCompatLog) << "Unsupported baud rate for portable serial backend:" << baudRate;
        return false;
    }
    return _applyTermios();
}

bool QSerialPort::setDataBits(DataBits dataBits)
{
    _dataBits = dataBits;
    return (_fd < 0) || _applyTermios();
}

bool QSerialPort::setParity(Parity parity)
{
    _parity = parity;
    return (_fd < 0) || _applyTermios();
}

bool QSerialPort::setStopBits(StopBits stopBits)
{
    _stopBits = stopBits;
    return (_fd < 0) || _applyTermios();
}

bool QSerialPort::setFlowControl(FlowControl flowControl)
{
    _flowControl = flowControl;
    return (_fd < 0) || _applyTermios();
}

bool QSerialPort::setDataTerminalReady(bool set)
{
    if (_fd < 0) {
        return false;
    }
    int status = 0;
    if (::ioctl(_fd, TIOCMGET, &status) != 0) {
        // Not all serial-like devices (e.g. a pseudo-terminal, used by the Q8e data-path
        // test) support modem control lines -- best-effort no-op rather than an error.
        return false;
    }
    if (set) {
        status |= TIOCM_DTR;
    } else {
        status &= ~TIOCM_DTR;
    }
    return (::ioctl(_fd, TIOCMSET, &status) == 0);
}

bool QSerialPort::flush()
{
    if (_fd < 0) {
        return false;
    }
    return (::tcdrain(_fd) == 0);
}

qint64 QSerialPort::bytesAvailable() const
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

qint64 QSerialPort::readData(char *data, qint64 maxSize)
{
    if (_fd < 0) {
        return -1;
    }
    const ssize_t n = ::read(_fd, data, static_cast<size_t>(maxSize));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        _setError(ReadError, QString::fromLocal8Bit(std::strerror(errno)));
        return -1;
    }
    return n;
}

qint64 QSerialPort::writeData(const char *data, qint64 maxSize)
{
    if (_fd < 0) {
        return -1;
    }

    // Blocking (poll-based) write loop: hand every byte to the kernel tty layer before
    // returning, so waitForBytesWritten() below always finds nothing pending. See the
    // header's "Design notes" for why this differs from real QSerialPort's async model.
    qint64 total = 0;
    QElapsedTimer timer;
    timer.start();
    while (total < maxSize) {
        const ssize_t n = ::write(_fd, data + total, static_cast<size_t>(maxSize - total));
        if (n > 0) {
            total += n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            const int remaining = kWriteTimeoutMs - static_cast<int>(timer.elapsed());
            if (remaining <= 0) {
                break;
            }
            struct pollfd pfd;
            pfd.fd = _fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            (void) ::poll(&pfd, 1, remaining);
            continue;
        }
        if (total == 0) {
            _setError(WriteError, QString::fromLocal8Bit(std::strerror(errno)));
            return -1;
        }
        break;
    }

    if (total > 0) {
        emit bytesWritten(total);
    }
    return total;
}

bool QSerialPort::waitForReadyRead(int msecs)
{
    if (_fd < 0) {
        return false;
    }
    struct pollfd pfd;
    pfd.fd = _fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    const int rc = ::poll(&pfd, 1, msecs);
    if (rc > 0 && (pfd.revents & POLLIN)) {
        emit readyRead();
        return true;
    }
    if (rc == 0) {
        _setError(TimeoutError, QStringLiteral("Timed out waiting for data"));
    }
    return false;
}

bool QSerialPort::waitForBytesWritten(int msecs)
{
    Q_UNUSED(msecs);
    // writeData() above is synchronous -- by the time write()/writeData() returns, all
    // bytes already reached the kernel. Nothing is ever left pending to wait for.
    return (_fd >= 0);
}

void QSerialPort::_handleReadyRead()
{
    emit readyRead();
}

/*===========================================================================*/

QSerialPortInfo::QSerialPortInfo() = default;

QSerialPortInfo::~QSerialPortInfo() = default;

QSerialPortInfo::QSerialPortInfo(const QSerialPort &port)
    : QSerialPortInfo(port.portName())
{
}

QSerialPortInfo::QSerialPortInfo(const QString &portName)
{
    const QString location = systemLocationForPortName(portName);
    // Same "/dev/"-stripped form QSerialPort::portName() reports (see setPortName).
    _portName = location;
    if (_portName.startsWith(QStringLiteral("/dev/"))) {
        _portName.remove(0, 5);
    }
    _systemLocation = location;

    // Best effort: pull VID/PID/description/manufacturer/serial off the concrete device
    // node from udev, so board detection (QGCSerialPortInfo::getBoardInfo) works the same
    // whether the caller went through availablePorts() or constructed directly from a
    // port/name. A tty with no udev-visible hardware behind it (e.g. a pty) simply keeps
    // the name-only fields -- matching real QSerialPortInfo, whose USB descriptor
    // accessors are also empty for such devices.
    const QList<QSerialPortInfo> all = availablePorts();
    for (const QSerialPortInfo &info : all) {
        if (info.systemLocation() == _systemLocation) {
            *this = info;
            return;
        }
    }
}

bool QSerialPortInfo::isBusy() const
{
    if (_systemLocation.isEmpty()) {
        return false;
    }
    const int fd = ::open(_systemLocation.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return (errno == EBUSY || errno == EACCES);
    }
    ::close(fd);
    return false;
}

QList<qint32> QSerialPortInfo::standardBaudRates()
{
    return {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
}

QList<QSerialPortInfo> QSerialPortInfo::availablePorts()
{
    QList<QSerialPortInfo> result;

    struct udev *udevContext = ::udev_new();
    if (!udevContext) {
        qCWarning(SerialCompatLog) << "udev_new() failed -- serial port enumeration unavailable";
        return result;
    }

    struct udev_enumerate *enumerate = ::udev_enumerate_new(udevContext);
    (void) ::udev_enumerate_add_match_subsystem(enumerate, "tty");
    (void) ::udev_enumerate_scan_devices(enumerate);

    struct udev_list_entry *devices = ::udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *entry;
    udev_list_entry_foreach(entry, devices) {
        const char *sysPath = ::udev_list_entry_get_name(entry);
        struct udev_device *dev = ::udev_device_new_from_syspath(udevContext, sysPath);
        if (!dev) {
            continue;
        }

        // Only real (hardware-backed) tty nodes have a parent device in sysfs -- this is
        // the same filter Qt's own Linux QSerialPortInfo backend uses to skip virtual ttys
        // (ptys, consoles) that have no physical device behind them.
        struct udev_device *parent = ::udev_device_get_parent(dev);
        const char *devNode = ::udev_device_get_devnode(dev);
        if (!parent || !devNode) {
            ::udev_device_unref(dev);
            continue;
        }

        // Walk up to the nearest USB device ancestor for VID/PID/serial/manufacturer --
        // matches how a USB-serial adapter's identity is actually attached in sysfs (the
        // tty node itself rarely carries these attributes directly). Devices returned by
        // udev_device_get_parent*() are owned by `dev` and must not be unref'd separately.
        struct udev_device *usbDevice = ::udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
        struct udev_device *descDevice = usbDevice ? usbDevice : dev;

        QSerialPortInfo info;
        info._systemLocation = QString::fromLocal8Bit(devNode);
        // Same "/dev/"-stripped form QSerialPort::portName() reports (see setPortName).
        info._portName = info._systemLocation;
        if (info._portName.startsWith(QStringLiteral("/dev/"))) {
            info._portName.remove(0, 5);
        }

        if (const char *vendor = ::udev_device_get_sysattr_value(descDevice, "manufacturer")) {
            info._manufacturer = QString::fromLocal8Bit(vendor);
        }
        if (const char *product = ::udev_device_get_sysattr_value(descDevice, "product")) {
            info._description = QString::fromLocal8Bit(product);
        }
        if (const char *serial = ::udev_device_get_sysattr_value(descDevice, "serial")) {
            info._serialNumber = QString::fromLocal8Bit(serial);
        }
        if (const char *vid = ::udev_device_get_sysattr_value(descDevice, "idVendor")) {
            bool ok = false;
            const uint value = QString::fromLocal8Bit(vid).toUInt(&ok, 16);
            if (ok) {
                info._vendorId = static_cast<quint16>(value);
                info._hasVendorId = true;
            }
        }
        if (const char *pid = ::udev_device_get_sysattr_value(descDevice, "idProduct")) {
            bool ok = false;
            const uint value = QString::fromLocal8Bit(pid).toUInt(&ok, 16);
            if (ok) {
                info._productId = static_cast<quint16>(value);
                info._hasProductId = true;
            }
        }

        result.append(info);
        ::udev_device_unref(dev);
    }

    ::udev_enumerate_unref(enumerate);
    ::udev_unref(udevContext);

    return result;
}
