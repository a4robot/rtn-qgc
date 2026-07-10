#include "SerialPortDataPathTest.h"

#ifdef Q_OS_ANDROID
#include "qserialport.h"
#else
#include <QtSerialPort/QSerialPort>
#endif

#include <QtCore/QElapsedTimer>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace {

/// Reads whatever is available on a raw fd within timeoutMs, polling until either `expected`
/// bytes have arrived or the timeout elapses. Used on the pty MASTER side, which the test
/// drives directly via POSIX calls (it plays the role of "the physical device" the
/// QSerialPort-under-test is talking to).
QByteArray readFromRawFd(int fd, qsizetype expected, int timeoutMs)
{
    QByteArray result;
    QElapsedTimer timer;
    timer.start();

    while (result.size() < expected && timer.elapsed() < timeoutMs) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0 || ::poll(&pfd, 1, remaining) <= 0) {
            break;
        }
        char buf[4096];
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            result.append(buf, static_cast<int>(n));
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        }
    }

    return result;
}

/// Drains the port's pending write buffer onto the wire while simultaneously reading the
/// master fd, until `expected` bytes have arrived or `timeoutMs` elapses.
///
/// Characterized from the real Qt backend (first w17 run): QSerialPort::write() queues into
/// an internal buffer and only performs the actual write(2) from waitForBytesWritten()/the
/// event loop -- AND a pty's kernel buffer is small (~8-64KB), so for large payloads the
/// flush stalls until the master side is drained. Draining and flushing must therefore be
/// interleaved from the single test thread or the two sides deadlock waiting on each other.
/// The portable backend needs neither (its write() is fully synchronous) but is not harmed
/// by either call -- this helper is backend-agnostic on purpose.
QByteArray drainAndRead(QSerialPort &port, int masterFd, qsizetype expected, int timeoutMs)
{
    QByteArray result;
    QElapsedTimer timer;
    timer.start();

    while (result.size() < expected && timer.elapsed() < timeoutMs) {
        if (port.bytesToWrite() > 0) {
            (void) port.waitForBytesWritten(50);
        }
        result += readFromRawFd(masterFd, expected - result.size(), 50);
    }

    return result;
}

} // namespace

void SerialPortDataPathTest::init()
{
    // posix_openpt/grantpt/unlockpt/ptsname: the POSIX (no external tooling, e.g. no socat)
    // way to create a pty pair -- the wave-16 baseline's gap this test closes. The slave
    // path is what QSerialPort::setPortName()/open() will be pointed at; the master fd is
    // driven directly via raw POSIX calls to play "the device" on the other end.
    _masterFd = ::posix_openpt(O_RDWR | O_NOCTTY);
    QVERIFY2(_masterFd >= 0, "posix_openpt failed -- pty support required for this test");
    QVERIFY2(::grantpt(_masterFd) == 0, "grantpt failed");
    QVERIFY2(::unlockpt(_masterFd) == 0, "unlockpt failed");

    const char *name = ::ptsname(_masterFd);
    QVERIFY2(name != nullptr, "ptsname failed");
    _slavePath = QString::fromLocal8Bit(name);
}

void SerialPortDataPathTest::cleanup()
{
    if (_masterFd >= 0) {
        ::close(_masterFd);
        _masterFd = -1;
    }
    _slavePath.clear();
}

void SerialPortDataPathTest::_testOpenClose()
{
    QSerialPort port;
    port.setPortName(_slavePath);

    // Characterized from the real Qt backend (first w17 run): setPortName("/dev/pts/N")
    // reports portName() as "pts/N" -- QSerialPort strips exactly the "/dev/" prefix on
    // Linux (not a full basename). The portable backend must reproduce this: LinkManager's
    // _checkPortAvailability and SerialConfiguration::cleanPortDisplayName compare
    // portName() forms across QSerialPort and QSerialPortInfo, so both backends must agree.
    QString expectedPortName = _slavePath;
    if (expectedPortName.startsWith(QStringLiteral("/dev/"))) {
        expectedPortName.remove(0, 5);
    }
    QCOMPARE(port.portName(), expectedPortName);

    QVERIFY2(port.open(QIODevice::ReadWrite), qPrintable(port.errorString()));
    QVERIFY(port.isOpen());

    port.close();
    QVERIFY(!port.isOpen());
}

void SerialPortDataPathTest::_testDeviceToHostEcho()
{
    QSerialPort port;
    port.setPortName(_slavePath);
    QVERIFY2(port.open(QIODevice::ReadWrite), qPrintable(port.errorString()));

    QSignalSpy readySpy(&port, &QSerialPort::readyRead);

    const QByteArray sent = QByteArrayLiteral("\x01\x02\xFEMAVLINK-ish-payload\x00trailing");
    QCOMPARE(::write(_masterFd, sent.constData(), sent.size()), static_cast<ssize_t>(sent.size()));

    QVERIFY2(UnitTest::waitForSignalCount(readySpy, 1, TestTimeout::mediumMs(), QStringLiteral("QSerialPort::readyRead")),
              "readyRead never fired for device->host bytes");

    const QByteArray received = port.readAll();
    QCOMPARE(received, sent);
}

void SerialPortDataPathTest::_testHostToDeviceEcho()
{
    QSerialPort port;
    port.setPortName(_slavePath);
    QVERIFY2(port.open(QIODevice::ReadWrite), qPrintable(port.errorString()));

    const QByteArray sent = QByteArrayLiteral("host-to-device\x00\xFF\x7Fpayload");
    const qint64 written = port.write(sent);
    QCOMPARE(written, static_cast<qint64>(sent.size()));
    // Best-effort only: real QSerialPort returns false here when nothing is left buffered
    // (i.e. it already wrote everything synchronously), while the portable backend's
    // writeData() is always fully synchronous by construction -- see
    // _testLargeWritePartialCompletionIsConsistent for why this test doesn't assert either
    // way. The actual correctness gate is the byte-for-byte compare below.
    (void) port.waitForBytesWritten(TestTimeout::mediumMs());

    const QByteArray received = readFromRawFd(_masterFd, sent.size(), TestTimeout::mediumMs());
    QCOMPARE(received, sent);
}

void SerialPortDataPathTest::_testSequentialWritesPreserveOrder()
{
    QSerialPort port;
    port.setPortName(_slavePath);
    QVERIFY2(port.open(QIODevice::ReadWrite), qPrintable(port.errorString()));

    const QList<QByteArray> chunks = {
        QByteArrayLiteral("AAAA"),
        QByteArrayLiteral("BBBBBB"),
        QByteArrayLiteral("C"),
        QByteArrayLiteral("DDDDDDDDDD"),
    };

    qint64 totalExpected = 0;
    for (const QByteArray &chunk : chunks) {
        QCOMPARE(port.write(chunk), static_cast<qint64>(chunk.size()));
        totalExpected += chunk.size();
    }

    // drainAndRead (not a bare raw-fd read): the real Qt backend holds these bytes in its
    // internal write buffer until waitForBytesWritten()/the event loop flushes them -- see
    // the helper's comment for the characterization.
    const QByteArray received = drainAndRead(port, _masterFd, totalExpected, TestTimeout::mediumMs());
    QCOMPARE(received, chunks.join());
}

void SerialPortDataPathTest::_testLargeWritePartialCompletionIsConsistent()
{
    QSerialPort port;
    port.setPortName(_slavePath);
    QVERIFY2(port.open(QIODevice::ReadWrite), qPrintable(port.errorString()));

    // Comfortably larger than any Linux pty kernel buffer (a handful of KB), with nothing
    // draining the master side while write() runs. The two backends are ALLOWED to differ
    // in what write() returns here -- characterized from the first w17 run against the real
    // Qt backend, and pinned as documentation of the difference:
    //   - real QSerialPort accepts the full 1MB into its internal (unbounded) write buffer
    //     immediately and flushes it incrementally from waitForBytesWritten()/the event
    //     loop as the pty drains;
    //   - the portable backend (QGCSerialPortCompat.cc) writes+polls synchronously inside
    //     write() itself and returns a short count (roughly one pty buffer's worth) once
    //     its internal ~2s timeout elapses with nothing draining the master side.
    // Either is a legitimate implementation choice; SerialWorker::writeData() (the only
    // MAVLink-path caller) already loops on short counts. What Q8e actually needs pinned is
    // the CORRECTNESS invariant that survives the swap: whatever count write() reports,
    // exactly that many bytes eventually show up on the wire, byte-identical and in order
    // -- nothing dropped, duplicated, or reordered.
    QByteArray big(1024 * 1024, '\0');
    for (int i = 0; i < big.size(); ++i) {
        big[i] = static_cast<char>(i & 0xFF);
    }

    const qint64 written = port.write(big);
    QVERIFY2(written > 0, "write() must make at least some progress");

    const QByteArray received = drainAndRead(port, _masterFd, written, TestTimeout::longMs());
    QCOMPARE(received, big.left(static_cast<int>(written)));
}

void SerialPortDataPathTest::_testConfigurationSettersNoOpOnPty()
{
    QSerialPort port;
    port.setPortName(_slavePath);
    QVERIFY2(port.open(QIODevice::ReadWrite), qPrintable(port.errorString()));

    // All of these must report success (the ioctl/termios call itself succeeds on a pty)
    // even though a pty has no physical UART to actually apply them to -- documented
    // no-op, not a bug (QGCSerialPortCompat.h's design notes).
    QVERIFY(port.setBaudRate(QSerialPort::Baud115200));
    QVERIFY(port.setDataBits(QSerialPort::Data8));
    QVERIFY(port.setParity(QSerialPort::NoParity));
    QVERIFY(port.setStopBits(QSerialPort::OneStop));
    QVERIFY(port.setFlowControl(QSerialPort::NoFlowControl));

    // The data path must still work after reconfiguring -- proves the no-op is benign, not
    // silently corrupting port state.
    QSignalSpy readySpy(&port, &QSerialPort::readyRead);
    const QByteArray sent = QByteArrayLiteral("still-works-after-reconfig");
    QCOMPARE(::write(_masterFd, sent.constData(), sent.size()), static_cast<ssize_t>(sent.size()));
    QVERIFY(UnitTest::waitForSignalCount(readySpy, 1, TestTimeout::mediumMs(), QStringLiteral("QSerialPort::readyRead")));
    QCOMPARE(port.readAll(), sent);
}

UT_REGISTER_TEST(SerialPortDataPathTest, TestLabel::Integration, TestLabel::Comms)
